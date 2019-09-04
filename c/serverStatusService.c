

/*
  This program and the accompanying materials are
  made available under the terms of the Eclipse Public License v2.0 which accompanies
  this distribution, and is available at https://www.eclipse.org/legal/epl-v20.html
  
  SPDX-License-Identifier: EPL-2.0
  
  Copyright Contributors to the Zowe Project.
*/


#ifdef METTLE
#include <metal/metal.h>
#include <metal/stddef.h>
#include <metal/stdio.h>
#include <metal/stdlib.h>
#include <metal/string.h>
#include "metalio.h"
#include "qsam.h"
#else
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#endif
#include <sys/utsname.h>

#include "zowetypes.h"
#include "bpxnet.h"
#include "utils.h"
#include "socketmgmt.h"

#include "logging.h"
#include "zssLogging.h"
#include "serverStatusService.h"

#ifdef __ZOWE_OS_ZOS
static int serveStatus(HttpService *service, HttpResponse *response);
extern char **environ;

typedef int EXSMFI(int *reqType, int *recType, int *subType,
                   char* buffer, int *bufferLen, int *cpuUtil,
                   int *dpRate, int *options, int *mvs, int *zaap, int *ziip);
EXSMFI *smfFunc;

int installServerStatusService(HttpServer *server, JsonObject *serverSettings, char* productVer)
{
  HttpService *httpService = makeGeneratedService("Server_Status_Service", "/server/agent/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->serviceFunction = &serveStatus;
  httpService->runInSubtask = TRUE;
  httpService->doImpersonation = TRUE;
  ServerAgentContext *context = (ServerAgentContext*)safeMalloc(sizeof(ServerAgentContext), "ServerAgentContext");
  context->serverConfig = serverSettings;
  memcpy(context->productVersion, productVer, strlen(productVer));
  httpService->userPointer = context;
  registerHttpService(server, httpService);
  return 0;
}

void respondWithServerConfig(HttpResponse *response, JsonObject* config){
  jsonPrinter *out = respondWithJsonPrinter(response);
  setResponseStatus(response, 200, "OK");
  setDefaultJSONRESTHeaders(response);
  writeHeader(response);
  jsonStart(out);
  jsonStartObject(out, "options");
  jsonPrintObject(out, config);
  jsonEndObject(out);
  jsonEnd(out);
  finishResponse(response);
}

void respondWithServerRoutes(HttpResponse *response){
  jsonPrinter *out = respondWithJsonPrinter(response);
  setResponseStatus(response, 200, "OK");
  setDefaultJSONRESTHeaders(response);
  writeHeader(response);
  jsonStart(out);
  jsonStartArray(out, "links");
  jsonStartObject(out, NULL);
  jsonAddString(out, "href", "/server/agent/config");
  jsonAddString(out, "rel", "config");
  jsonAddString(out, "type", "GET");
  jsonEndObject(out);
  jsonStartObject(out, NULL);
  jsonAddString(out, "href", "/server/agent/log");
  jsonAddString(out, "rel", "log");
  jsonAddString(out, "type", "GET");
  jsonEndObject(out);
  jsonStartObject(out, NULL);
  jsonAddString(out, "href", "/server/agent/logLevels");
  jsonAddString(out, "rel", "logLevels");
  jsonAddString(out, "type", "GET");
  jsonEndObject(out);
  jsonStartObject(out, NULL);
  jsonAddString(out, "href", "/server/agent/environment");
  jsonAddString(out, "rel", "environment");
  jsonAddString(out, "type", "GET");
  jsonEndObject(out);
  jsonEndArray(out);
  jsonEnd(out);
  finishResponse(response);
}

void respondWithLogLevels(HttpResponse *response, ServerAgentContext *context){
  jsonPrinter *out = respondWithJsonPrinter(response);
  JsonObject *logLevels = jsonObjectGetObject(context->serverConfig, "logLevels");
  setResponseStatus(response, 200, "OK");
  setDefaultJSONRESTHeaders(response);
  writeHeader(response);
  jsonStart(out);
  if(logLevels){
    jsonPrintObject(out, logLevels);
  }
  jsonEnd(out);
  finishResponse(response);
}

void respondWithServerEnvironment(HttpResponse *response, ServerAgentContext *context){
  /*Information about parameters for smf_unc: https://www.ibm.com/support/knowledgecenter/SSLTBW_2.1.0/com.ibm.zos.v2r1.erbb700/smfp.htm#smfp*/
  struct utsname unameRet;
  uname(&unameRet);
  char smfVarBuffer[64];
  char* buffer;
  int rc = 0;
  int reqtype = 0x00000005; //fullword. request type
  int rectype = 0x0000004F; //SMF record type, only type 79 is supported
  int subtype = 0x00000009; //SMF record subtype
  int bufferlen = 716800; // Yes the SMF buffer is actually 700Kb roughly
  int cpuUtil = 0x00000000;
  int demandPaging = 0x00000000;
  int options = 0x00000000;
  int mvsSrm = 0x00000000;
  int zaapUtil = 0x00000000;
  int ziipUtil = 0x00000000;
  buffer = (char *)safeMalloc(bufferlen, "buffer");
  memset(buffer, 0, bufferlen);
  smfFunc = (EXSMFI *)fetch("ERBSMFI");
  rc = (*smfFunc)(&reqtype,
               &rectype,
               &subtype,
               buffer,
               &bufferlen,
               &cpuUtil,
               &demandPaging,
               &options,
               &mvsSrm,
               &zaapUtil,
               &ziipUtil);
  if(rc > 0){
    respondWithError(response, HTTP_STATUS_BAD_REQUEST, "Unable to fetch from RMF data interface service");
  }
  jsonPrinter *out = respondWithJsonPrinter(response);
  setResponseStatus(response, 200, "OK");
  setDefaultJSONRESTHeaders(response);
  writeHeader(response);
  jsonStart(out);
  jsonAddString(out, "logDirectory", getenv("ZSS_LOG_FILE"));
  jsonAddString(out, "agentName", "zss");
  jsonAddString(out, "agentVersion", context->productVersion);
  jsonAddString(out, "arch", unameRet.sysname);
  jsonAddString(out, "osRelease", unameRet.release);
  jsonAddString(out, "hardwareIdentifier", unameRet.machine);
  jsonAddString(out, "hostname", unameRet.nodename);
  jsonStartObject(out, "userEnvironment");
  char *env_var = *environ;
  int i = 1;
  for (; env_var; i++) {
    char *len = strchr(env_var, '=');
    if(len == NULL){
      break;
    }
    char *name = safeMalloc(strlen(env_var)-strlen(len) + 1, "env_var name");
    snprintf(name, sizeof(char)*(strlen(env_var)-strlen(len) + 1), "%s", env_var);
    jsonAddString(out, name, len+1);
    safeFree(name, strlen(name));
    env_var = *(environ+i);
  }
  jsonEndObject(out);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", demandPaging);
  jsonAddString(out, "demandPagingRate", smfVarBuffer);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", cpuUtil);
  jsonAddString(out, "stdCP_CPU_Util", smfVarBuffer);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", mvsSrm);
  jsonAddString(out, "stdCP_MVS_SRM_CPU_Util", smfVarBuffer);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", zaapUtil);
  jsonAddString(out, "ZAAP_CPU_Util", smfVarBuffer);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", ziipUtil);
  jsonAddString(out, "ZIIP_CPU_Util", smfVarBuffer);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", getpid());
  jsonAddString(out, "PID", smfVarBuffer);
  snprintf(smfVarBuffer, sizeof(smfVarBuffer), "%d", getppid());
  jsonAddString(out, "PPID", smfVarBuffer);
  jsonEnd(out);
  finishResponse(response);
}

static int serveStatus(HttpService *service, HttpResponse *response) {
  HttpRequest *request = response->request;
  ServerAgentContext *context = service->userPointer;
  JsonObject *dataserviceAuth = jsonObjectGetObject(context->serverConfig, "dataserviceAuthentication");
  int rbacParm = jsonObjectGetBoolean(dataserviceAuth, "rbac");
  if(!rbacParm){
     respondWithError(response, HTTP_STATUS_UNAUTHORIZED, "Unauthorized - RBAC is disabled.  Enable in zluxserver.json");
  } else {
    if(!strcmp(request->method, methodGET)){
      char *l1 = stringListPrint(request->parsedFile, 2, 1, "/", 0);
      if(!strcmp(l1, "")){
        respondWithServerRoutes(response);
      }else if (!strcmp(l1, "config")){
        respondWithServerConfig(response, context->serverConfig);
      }else if (!strcmp(l1, "log")) {
        char* logDir = getenv("ZSS_LOG_FILE");
        if(strcmp(logDir, "") || logDir == NULL){
          respondWithUnixFile2(NULL, response, logDir, 0, 0, false);
        } else {
           respondWithError(response, HTTP_STATUS_NOT_FOUND, "Log not found");
        }
      }else if (!strcmp(l1, "logLevels")){
        respondWithLogLevels(response, context);
      }else if (!strcmp(l1, "environment")){
        respondWithServerEnvironment(response, context);
      } else {
        respondWithJsonError(response, "Invalid path", 400, "Bad Request");
      }
    } else {
      jsonPrinter *out = respondWithJsonPrinter(response);
      setContentType(response, "text/json");
      setResponseStatus(response, 405, "Method Not Allowed");
      addStringHeader(response, "Server", "jdmfws");
      addStringHeader(response, "Transfer-Encoding", "chunked");
      addStringHeader(response, "Allow", "GET");
      writeHeader(response);
      jsonStart(out);
      jsonEnd(out);
      finishResponse(response);
    }
  }
  return 0;
}

#endif /* __ZOWE_OS_ZOS */


/*
  This program and the accompanying materials are
  made available under the terms of the Eclipse Public License v2.0 which accompanies
  this distribution, and is available at https://www.eclipse.org/legal/epl-v20.html
  
  SPDX-License-Identifier: EPL-2.0
  
  Copyright Contributors to the Zowe Project.
*/
