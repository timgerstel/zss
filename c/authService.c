

/*
  This program and the accompanying materials are
  made available under the terms of the Eclipse Public License v2.0 which accompanies
  this distribution, and is available at https://www.eclipse.org/legal/epl-v20.html
  
  SPDX-License-Identifier: EPL-2.0
  
  Copyright Contributors to the Zowe Project.
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <iconv.h>
#include <dirent.h>
#include <pthread.h>

#include "authService.h"
#include "zowetypes.h"
#include "alloc.h"
#include "utils.h"
#ifdef __ZOWE_OS_ZOS
#include "zos.h"
#endif
#include "logging.h"
#include "json.h"
#include "bpxnet.h"
#include "socketmgmt.h"
#include "zis/client.h"
#include "httpserver.h"

#define SAF_CLASS "ZOWE"

/*
 * A handler performing the SAF_AUTH check: checks if the user has the
 * specified access to the specified entity in the specified class
 *
 * URL format:
 *   GET .../saf-auth/<USERID>/<CLASS>/<ENTITY>/<READ|UPDATE|ALTER|CONTROL>
 * Example: /saf-auth/PDUSR/FACILITY/CQM.CAE.ADMINISTRATOR/READ
 *
 * Response examples:
 *  - The user is authorized: { "authorized": true }
 *  - Not authorized: { "authorized": false, message: "..." }
 *  - Error: {
 *      "error": true,
 *      "message": "..."
 *    }
 */

static int serveAuthCheck(HttpService *service, HttpResponse *response);

int installAuthCheckService(HttpServer *server) {
//  zowelog(NULL, 0, ZOWE_LOG_DEBUG2, "begin %s\n",
//  __FUNCTION__);
  HttpService *httpService = makeGeneratedService("SAF_AUTH service",
      "/saf-auth/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->serviceFunction = &serveAuthCheck;
  httpService->runInSubtask = FALSE;
  registerHttpService(server, httpService);
//  zowelog(NULL, 0, ZOWE_LOG_DEBUG2, "end %s\n",
//  __FUNCTION__);
  return 0;
}

static int extractQuery(StringList *path, char **entity, char **access) {
  const StringListElt *pathElt;

#define TEST_NEXT_AND_SET($ptr) do { \
  pathElt = pathElt->next;           \
  if (pathElt == NULL) {             \
    return -1;                       \
  }                                  \
  *$ptr = pathElt->string;           \
} while (0)

  pathElt = firstStringListElt(path);
  while (pathElt && (strcmp(pathElt->string, "saf-auth") != 0)) {
    pathElt = pathElt->next;
  }
  if (pathElt == NULL) {
    return -1;
  }
  TEST_NEXT_AND_SET(entity);
  TEST_NEXT_AND_SET(access);
  return 0;
#undef TEST_NEXT_AND_SET
}

static int parseAcess(const char inStr[], int *outNum) {
  int rc;

  if (strcasecmp("ALTER", inStr) == 0) {
    *outNum = SAF_AUTH_ATTR_ALTER;
  } else if (strcasecmp("CONTROL", inStr) == 0) {
    *outNum = SAF_AUTH_ATTR_CONTROL;
  } else if (strcasecmp("UPDATE", inStr) == 0) {
    *outNum = SAF_AUTH_ATTR_UPDATE;
  } else if (strcasecmp("READ", inStr) == 0) {
    *outNum = SAF_AUTH_ATTR_READ;
  } else {
    return -1;
  }
  return 0;
}

static void respond(HttpResponse *res, int rc, const ZISAuthServiceStatus
    *reqStatus) {
  jsonPrinter* p = respondWithJsonPrinter(res);

  setResponseStatus(res, HTTP_STATUS_OK, "OK");
  setDefaultJSONRESTHeaders(res);
  writeHeader(res);
  if (rc == RC_ZIS_SRVC_OK) {
    jsonStart(p); {
      jsonAddBoolean(p, "authorized", true);
    }
    jsonEnd(p);
  } else {
    char errBuf[0x100];

#define FORMAT_ERROR($fmt, ...) snprintf(errBuf, sizeof (errBuf), $fmt, \
    ##__VA_ARGS__)

    ZIS_FORMAT_AUTH_CALL_STATUS(rc, reqStatus, FORMAT_ERROR);
#undef FORMAT_ERROR
    jsonStart(p); {
      if (rc == RC_ZIS_SRVC_SERVICE_FAILED
          && reqStatus->safStatus.safRC != 0) {
        jsonAddBoolean(p, "authorized", false);
      } else {
        jsonAddBoolean(p, "error", true);
      }
      jsonAddString(p, "message", errBuf);
    }
    jsonEnd(p);
  }
  finishResponse(res);
}

static int serveAuthCheck(HttpService *service, HttpResponse *res) {
  HttpRequest *req = res->request;
  char *entity, *accessStr;
  int access = 0;
  int rc = 0, rsn = 0, safStatus = 0;
  ZISAuthServiceStatus reqStatus = {0};
  CrossMemoryServerName *privilegedServerName;
  const char *userName = req->username, *class = SAF_CLASS;

  rc = extractQuery(req->parsedFile, &entity, &accessStr);
  if (rc != 0) {
    respondWithError(res, HTTP_STATUS_BAD_REQUEST, "Broken auth query");
    return 0;
  }
  rc = parseAcess(accessStr, &access);
  if (rc != 0) {
    respondWithError(res, HTTP_STATUS_BAD_REQUEST, "Unexpected access level");
    return 0;
  }
  /* printf("query: user %s, class %s, entity %s, access %d\n", userName, class,
      entity, access); */
  privilegedServerName = getConfiguredProperty(service->server,
      HTTP_SERVER_PRIVILEGED_SERVER_PROPERTY);
  rc = zisCheckEntity(privilegedServerName, userName, class, entity, access,
      &reqStatus);
  respond(res, rc, &reqStatus);
  return 0;
}


/*
  This program and the accompanying materials are
  made available under the terms of the Eclipse Public License v2.0 which accompanies
  this distribution, and is available at https://www.eclipse.org/legal/epl-v20.html
  
  SPDX-License-Identifier: EPL-2.0
  
  Copyright Contributors to the Zowe Project.
*/

