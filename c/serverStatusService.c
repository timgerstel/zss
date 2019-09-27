

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
#include "timeutls.h"
#include "time.h"
#include "json.h"
#include "httpserver.h"
#include "logging.h"
#include "zssLogging.h"
#include "serverStatusService.h"

#ifdef __ZOWE_OS_ZOS
static int serveStatus(HttpService *service, HttpResponse *response);

typedef int EXSMFI(int *reqType, int *recType, int *subType,
                   char* buffer, int *bufferLen, int *cpuUtil,
                   int *dpRate, int *options, int *mvs, int *zaap, int *ziip);
EXSMFI *smfFunc;

void installServerStatusService(HttpServer *server, JsonObject *serverSettings, char* productVer)
{
  HttpService *httpService = makeGeneratedService("Server_Status_Service", "/server/agent/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->serviceFunction = &serveStatus;
  httpService->runInSubtask = TRUE;
  httpService->doImpersonation = TRUE;
  ServerAgentContext *context = (ServerAgentContext*)safeMalloc(sizeof(ServerAgentContext), "ServerAgentContext");
  if(context != NULL){
    context->serverConfig = serverSettings;
    context->productVersion[sizeof(context->productVersion) - 1] = '\0';
    strncpy(context->productVersion, productVer, sizeof(context->productVersion) - 1);
  }
  httpService->userPointer = context;
  registerHttpService(server, httpService);
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

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif
void respondWithServerEnvironment(HttpResponse *response, ServerAgentContext *context){
  /*Information about parameters for smf_unc: https://www.ibm.com/support/knowledgecenter/SSLTBW_2.1.0/com.ibm.zos.v2r1.erbb700/smfp.htm#smfp*/
  extern char **environ;
  struct utsname unameRet;
  char hostnameBuffer[HOST_NAME_MAX];
  char *buffer;
  int rc = 0;
  int reqtype = 0x00000005; //fullword. request type
  int rectype = 0x0000004F; //SMF record type, only type 79 is supported
  int subtype = 0x00000009; //SMF record subtype
  int bufferlen = 716800; // Yes the SMF buffer is actually 700Kb roughly
  int cpuUtil = 0, demandPaging = 0, options = 0, mvsSrm = 0, zaapUtil = 0, ziipUtil = 0;
  uname(&unameRet);
  gethostname(hostnameBuffer, sizeof(hostnameBuffer));
  buffer = (char *)safeMalloc(bufferlen, "buffer");
  if(buffer == NULL){
    respondWithError(response, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to allocate resources");
    return;
  }
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
  safeFree(buffer, bufferlen);
  if(rc > 0){
    respondWithError(response, HTTP_STATUS_BAD_REQUEST, "Unable to fetch from RMF data interface service");
    printf("Failed to fetch RMF data, RC = %d\n", rc);
    return;
  }
  time_t ltime;
  char tstamp[50];
  time(&ltime);
  jsonPrinter *out = respondWithJsonPrinter(response);
  setResponseStatus(response, 200, "OK");
  setDefaultJSONRESTHeaders(response);
  writeHeader(response);
  jsonStart(out);
  if(ctime_r(&ltime, tstamp) != NULL){
    if(tstamp[strlen(tstamp) - 1] != '\0'){
      tstamp[strlen(tstamp) - 1] = '\0';
    }
    jsonAddString(out, "timestamp", tstamp);
  }
  if(getenv("ZSS_LOG_FILE") != NULL){
    jsonAddString(out, "logDirectory", getenv("ZSS_LOG_FILE"));
  } else {
    jsonAddString(out, "logDirectory", "ZSS_LOG_FILE not defined in environment variables");
  }
  jsonAddString(out, "agentName", "zss");
  jsonAddString(out, "agentVersion", context->productVersion);
  jsonAddString(out, "arch", unameRet.sysname);
  jsonAddString(out, "osRelease", unameRet.release);
  jsonAddString(out, "osVersion", unameRet.version);
  jsonAddString(out, "hardwareIdentifier", unameRet.machine);
  jsonAddString(out, "hostname", hostnameBuffer);
  jsonAddString(out, "nodename", unameRet.nodename);
  jsonStartObject(out, "userEnvironment");
  char *envVar = *environ;
  for (int i = 1; envVar; i++) {
    char *equalSign = strchr(envVar, '=');
    if(equalSign == NULL){
      break;
    }
    int nameLen = strlen(envVar) - strlen(equalSign);
    int nameBufferLen = nameLen + 1;
    char *name = safeMalloc(nameBufferLen, "env_var name");
    if(name == NULL){
      break;
    }
    memcpy(name, envVar, nameLen);
    name[nameLen] = '\0';
    jsonAddString(out, name, equalSign+1);
    safeFree(name, nameBufferLen);
    envVar = *(environ+i);
  }
  jsonEndObject(out);
  jsonAddInt(out, "demandPagingRate", demandPaging);
  jsonAddInt(out, "stdCP_CPU_Util", cpuUtil);
  jsonAddInt(out, "stdCP_MVS_SRM_CPU_Util", mvsSrm);
  jsonAddInt(out, "ZAAP_CPU_Util", zaapUtil);
  jsonAddInt(out, "ZIIP_CPU_Util", ziipUtil);
  jsonAddInt(out, "PID", getpid());
  jsonAddInt(out, "PPID", getppid());
  jsonEnd(out);
  finishResponse(response);
}

static int serveStatus(HttpService *service, HttpResponse *response) {
  HttpRequest *request = response->request;
  ServerAgentContext *context = service->userPointer;
  //This service is conditional on RBAC being enabled because it is a 
  //sensitive URL that only RBAC authorized users should be able to access
  JsonObject *dataserviceAuth = jsonObjectGetObject(context->serverConfig, "dataserviceAuthentication");
  int rbacParm = jsonObjectGetBoolean(dataserviceAuth, "rbac");
  if(!rbacParm){
     respondWithError(response, HTTP_STATUS_BAD_REQUEST, "Set dataserviceAuthentication.rbac to true in server configuration");
  } else {
    if(!strcmp(request->method, methodGET)){
      char *l1 = stringListPrint(request->parsedFile, 2, 1, "/", 0);
      if(!strcmp(l1, "")){
        respondWithServerRoutes(response);
      }else if (!strcmp(l1, "config")){
        respondWithServerConfig(response, context->serverConfig);
      }else if (!strcmp(l1, "log")) {
        char* logDir = getenv("ZSS_LOG_FILE");
        if(logDir == NULL || strcmp(logDir, "")){
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

