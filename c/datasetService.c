

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

#include "zowetypes.h"
#include "isgenq.h"
#include "alloc.h"
#include "bpxnet.h"
#include "zos.h"
#include "utils.h"
#include "socketmgmt.h"
#include "dynalloc.h"

#include "httpserver.h"
#include "dataservice.h"
#include "json.h"
#include "datasetjson.h"
#include "logging.h"
#include "zssLogging.h"

#include "datasetService.h"


#ifdef __ZOWE_OS_ZOS

#define IS_DAMEMBER_EMPTY($member) \
  (!memcmp(&($member), &(DynallocMemberName){"        "}, sizeof($member)))

ENQToken tempLock;
DynallocDatasetName daDsn = {0};
DynallocMemberName daMember = {0};
DynallocDDName daDDname = {.name = "????????"};

static int serveDatasetMetadata(HttpService *service, HttpResponse *response) {
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);
  HttpRequest *request = response->request;
  if (!strcmp(request->method, methodGET)) {
    if (service->userPointer == NULL){
      MetadataQueryCache *userData = (MetadataQueryCache*)safeMalloc(sizeof(MetadataQueryCache),"Pointer to metadata cache");
      service->userPointer = userData;
    }

    char *l1 = stringListPrint(request->parsedFile, 1, 1, "/", 0); //expect name or hlq
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "l1=%s\n", l1);
    if (!strcmp(l1, "name")){
      respondWithDatasetMetadata(response);
    }
    else if (!strcmp(l1, "hlq")) {
      respondWithHLQNames(response,(MetadataQueryCache*)service->userPointer);
    }
    else {
      respondWithJsonError(response, "Invalid Subpath", 400, "Bad Request");
    }
  }
  else{
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

  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "end %s\n", __FUNCTION__);
  return 0;
}

static int serveDatasetContents(HttpService *service, HttpResponse *response){
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);
  HttpRequest *request = response->request;

  if (!strcmp(request->method, methodGET)) {
    char *l1 = stringListPrint(request->parsedFile, 1, 1, "/", 0);
    char *percentDecoded = cleanURLParamValue(response->slh, l1);
    char *filenamep1 = stringConcatenate(response->slh, "//'", percentDecoded);
    char *filename = stringConcatenate(response->slh, filenamep1, "'");
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Serving: %s\n", filename);
    fflush(stdout);
    respondWithDataset(response, filename, TRUE);
  }
  else if (!strcmp(request->method, methodPOST)){
    char *l1 = stringListPrint(request->parsedFile, 1, 1, "/", 0);
    char *percentDecoded = cleanURLParamValue(response->slh, l1);
    char *filenamep1 = stringConcatenate(response->slh, "//'", percentDecoded);
    char *filename = stringConcatenate(response->slh, filenamep1, "'");
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Updating if exists: %s\n", filename);
    fflush(stdout);
    updateDataset(response, filename, TRUE);

  }
  else if (!strcmp(request->method, methodDELETE)) {
    char *l1 = stringListPrint(request->parsedFile, 1, 1, "/", 0);
    char *percentDecoded = cleanURLParamValue(response->slh, l1);
    char *filenamep1 = stringConcatenate(response->slh, "//'", percentDecoded);
    char *filename = stringConcatenate(response->slh, filenamep1, "'");
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Deleting if exists: %s\n", filename);
    fflush(stdout);
    deleteDatasetOrMember(response, filename);
  }
  else {
    jsonPrinter *out = respondWithJsonPrinter(response);

    setContentType(response, "text/json");
    setResponseStatus(response, 405, "Method Not Allowed");
    addStringHeader(response, "Server", "jdmfws");
    addStringHeader(response, "Transfer-Encoding", "chunked");
    addStringHeader(response, "Allow", "GET, DELETE, POST");
    writeHeader(response);

    jsonStart(out);
    jsonEnd(out);

    finishResponse(response);
  }
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "end %s\n", __FUNCTION__);
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Returning from servedatasetcontents\n");
  return 0;
}

static int serveVSAMDatasetContents(HttpService *service, HttpResponse *response){
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);
  HttpRequest *request = response->request;
  serveVSAMCache *cache = (serveVSAMCache *)service->userPointer;
  if (!strcmp(request->method, methodGET)) {
    char *filename = stringListPrint(request->parsedFile, 1, 1, "/", 0);
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Serving: %s\n", filename);
    fflush(stdout);
    respondWithVSAMDataset(response, filename, cache->acbTable, TRUE);
  }
  else if (!strcmp(request->method, methodPOST)){
    char *filename = stringListPrint(request->parsedFile, 1, 1, "/", 0);
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Updating if exists: %s\n", filename);
    fflush(stdout);
    updateVSAMDataset(response, filename, cache->acbTable, TRUE);
  }
  else if (!strcmp(request->method, methodDELETE)) {
    char *l1 = stringListPrint(request->parsedFile, 1, 1, "/", 0);
    char *percentDecoded = cleanURLParamValue(response->slh, l1);
    char *filenamep1 = stringConcatenate(response->slh, "//'", percentDecoded);
    char *filename = stringConcatenate(response->slh, filenamep1, "'");
    zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Deleting if exists: %s\n", filename);
    fflush(stdout);
    deleteVSAMDataset(response, filename);
  }
  else {
    jsonPrinter *out = respondWithJsonPrinter(response);

    setContentType(response, "text/json");
    setResponseStatus(response, 405, "Method Not Allowed");
    addStringHeader(response, "Server", "jdmfws");
    addStringHeader(response, "Transfer-Encoding", "chunked");
    addStringHeader(response, "Allow", "GET, DELETE, POST");
    writeHeader(response);

    jsonStart(out);
    jsonEnd(out);

    finishResponse(response);
  }
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "end %s\n", __FUNCTION__);
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Returning from serveVSAMdatasetcontents\n");
  return 0;
}

static int serveDatasetLockingService(HttpService *service, HttpResponse *response) {
  HttpRequest *request = response->request;
  char *l1 = stringListPrint(request->parsedFile, 1, 1, "/", 0);
  char *percentDecoded = cleanURLParamValue(response->slh, l1);
  char *absPathTemp = stringConcatenate(response->slh, "//'", percentDecoded);
  char *absDsPath = stringConcatenate(response->slh, absPathTemp, "'");
  char *username = request->username;
  printf("percent decoded datasetName: %s\n", percentDecoded);
  if(!strcmp(percentDecoded, "")) {
    respondWithJsonError(response, "Malformed request URL, no dataset name provided", 400, "Bad Request");
    return -1;
  } else if(!isDatasetPathValid(absDsPath)){
    respondWithError(response,HTTP_STATUS_BAD_REQUEST,"Invalid dataset path");
    return -1;
  }
  DatasetName dsnName;
  DatasetMemberName memName;
  extractDatasetAndMemberName(absDsPath, &dsnName, &memName);
  nullTerminate(dsnName.value, sizeof(dsnName.value));
  nullTerminate(memName.value, sizeof(memName.value));
  printf("Extracted dsName: %s\nExtracted member: %s\n", dsnName.value, memName.value);
  int daRC = RC_DYNALLOC_OK, daSysRC = 0, daSysRSN = 0;
  memset(daDsn.name, ' ', sizeof(daDsn.name));
  memset(daMember.name, ' ', sizeof(daMember.name));
  memcpy(daDsn.name, percentDecoded, strlen(percentDecoded));
  memcpy(daMember.name, "", 0);
  if(!strcmp(request->method, methodPOST)) {
     daRC = dynallocAllocDataset(
      &daDsn,
      IS_DAMEMBER_EMPTY(daMember) ? NULL : &daMember,
      &daDDname,
      DYNALLOC_DISP_SHR,
      DYNALLOC_ALLOC_FLAG_NO_CONVERSION | DYNALLOC_ALLOC_FLAG_NO_MOUNT,
      &daSysRC, &daSysRSN
    );
    if(daRC != RC_DYNALLOC_OK) {
      printf("error: ds alloc dsn=\'%44.44s\', member=\'%8.8s\', dd=\'%8.8s\',"
              " rc=%d sysRC=%d, sysRSN=0x%08X (update)\n",
              daDsn.name, daMember.name, daDDname.name, daRC, daSysRC, daSysRSN);
       respondWithJsonError(response, "Unable to lock dataset", 400, "Bad Request");
      return -1;
    }
    jsonPrinter *out = respondWithJsonPrinter(response);
    setResponseStatus(response, 200, "OK");
    setDefaultJSONRESTHeaders(response);
    writeHeader(response);
    jsonStart(out);
    jsonAddString(out, NULL, "locked dataset");
    jsonEnd(out);
    finishResponse(response);
    return 1;
  } else if(!strcmp(request->method, methodDELETE)) {
    daRC = dynallocUnallocDatasetByDDName(&daDDname, DYNALLOC_UNALLOC_FLAG_NONE,
                                          &daSysRC, &daSysRSN);
    if (daRC != RC_DYNALLOC_OK) {
      printf("error: ds unalloc dsn=\'%44.44s\', member=\'%8.8s\', dd=\'%8.8s\',"
            " rc=%d sysRC=%d, sysRSN=0x%08X (read)\n",
            daDsn.name, daMember.name, daDDname.name, daRC, daSysRC, daSysRSN);
      respondWithJsonError(response, "Unable to unlock dataset", 400, "Bad Request");
      return -1;
    }
    jsonPrinter *out = respondWithJsonPrinter(response);
    setResponseStatus(response, 200, "OK");
    setDefaultJSONRESTHeaders(response);
    writeHeader(response);
    jsonStart(out);
    jsonAddString(out, NULL, "unlocked dataset");
    jsonEnd(out);
    finishResponse(response);
    return 1;
  } else {
    jsonPrinter *out = respondWithJsonPrinter(response);
    setContentType(response, "text/json");
    setResponseStatus(response, 405, "Method Not Allowed");
    addStringHeader(response, "Server", "jdmfws");
    addStringHeader(response, "Transfer-Encoding", "chunked");
    addStringHeader(response, "Allow", "POST, DELETE");
    writeHeader(response);
    jsonStart(out);
    jsonAddString(out, "405", "Method Not Allowed.");
    jsonEnd(out);
    finishResponse(response);
    return -1;
  }
  return 0;
}

void installDatasetLockingService(HttpServer *server) {
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Installing dataset writing service\n");
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);

  HttpService *httpService = makeGeneratedService("writeDataset", "/lockDataset/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->runInSubtask = TRUE;
  httpService->doImpersonation = TRUE;
  httpService->serviceFunction = serveDatasetLockingService;
  registerHttpService(server, httpService);
}

void installDatasetContentsService(HttpServer *server) {
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Installing dataset contents service\n");
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);

  HttpService *httpService = makeGeneratedService("datasetContents", "/datasetContents/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->runInSubtask = TRUE;
  httpService->doImpersonation = TRUE;
  httpService->serviceFunction = serveDatasetContents;
  registerHttpService(server, httpService);
}

void installVSAMDatasetContentsService(HttpServer *server) {
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Installing VSAM dataset contents service\n");
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);

  HttpService *httpService = makeGeneratedService("VSAMdatasetContents", "/VSAMdatasetContents/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->runInSubtask = TRUE;
  httpService->doImpersonation = TRUE;
  httpService->serviceFunction = serveVSAMDatasetContents;
  /* TODO: add VSAM params */
  httpService->paramSpecList = makeStringParamSpec("closeAfter",SERVICE_ARG_OPTIONAL, NULL);
  serveVSAMCache *cache = (serveVSAMCache *) safeMalloc(sizeof(serveVSAMCache), "Pointer to VSAM Cache");
  cache->acbTable = htCreate(0x2000,stringHash,stringCompare,NULL,NULL);
  httpService->userPointer = cache;
  registerHttpService(server, httpService);
}

void installDatasetMetadataService(HttpServer *server) {
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_INFO, "Installing dataset metadata service\n");
  zowelog(NULL, LOG_COMP_ID_MVD_SERVER, ZOWE_LOG_DEBUG2, "begin %s\n", __FUNCTION__);


  HttpService *httpService = makeGeneratedService("datasetMetadata", "/datasetMetadata/**");
  httpService->authType = SERVICE_AUTH_NATIVE_WITH_SESSION_TOKEN;
  httpService->runInSubtask = TRUE;
  httpService->doImpersonation = TRUE;
  httpService->serviceFunction = serveDatasetMetadata;
  httpService->paramSpecList =
    makeStringParamSpec("types", SERVICE_ARG_OPTIONAL,
      makeStringParamSpec("detail", SERVICE_ARG_OPTIONAL,
        makeStringParamSpec("listMembers", SERVICE_ARG_OPTIONAL,
          makeStringParamSpec("includeMigrated", SERVICE_ARG_OPTIONAL,
            makeStringParamSpec("updateCache", SERVICE_ARG_OPTIONAL,
              makeStringParamSpec("resumeName", SERVICE_ARG_OPTIONAL,
                makeStringParamSpec("resumeCatalogName", SERVICE_ARG_OPTIONAL,
                  makeStringParamSpec("includeUnprintable", SERVICE_ARG_OPTIONAL,
                    makeIntParamSpec("workAreaSize", SERVICE_ARG_OPTIONAL, 0,0,0,0, NULL)))))))));
  registerHttpService(server, httpService);
}

#endif /* __ZOWE_OS_ZOS */


/*
  This program and the accompanying materials are
  made available under the terms of the Eclipse Public License v2.0 which accompanies
  this distribution, and is available at https://www.eclipse.org/legal/epl-v20.html
  
  SPDX-License-Identifier: EPL-2.0
  
  Copyright Contributors to the Zowe Project.
*/

