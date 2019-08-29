/*
  This program and the accompanying materials are
  made available under the terms of the Eclipse Public License v2.0 which accompanies
  this distribution, and is available at https://www.eclipse.org/legal/epl-v20.html
  
  SPDX-License-Identifier: EPL-2.0
  
  Copyright Contributors to the Zowe Project.
*/

#ifndef __ZOWE_SYSTEM_SERVICES__
#define __ZOWE_SYSTEM_SERVICES__
#include "unixfile.h"
#include "bpxnet.h"
#include "httpserver.h"
#include "logging.h"
#include "zssLogging.h"

typedef int (*TraceSetFunction)(int toWhat);

typedef struct TraceDefinition_tag {
  const char* name;
  TraceSetFunction function;
} TraceDefinition;

int setMVDTrace(int toWhat);
JsonObject* getServerConfig();
char* getServerProductVersion();
static TraceDefinition traceDefs[] = {
  {"_zss.traceLevel", setMVDTrace},
  {"_zss.fileTrace", setFileTrace},
  {"_zss.socketTrace", setSocketTrace},
  {"_zss.httpParseTrace", setHttpParseTrace},
  {"_zss.httpDispatchTrace", setHttpDispatchTrace},
  {"_zss.httpHeadersTrace", setHttpHeadersTrace},
  {"_zss.httpSocketTrace", setHttpSocketTrace},
  {"_zss.httpCloseConversationTrace", setHttpCloseConversationTrace},
  {"_zss.httpAuthTrace", setHttpAuthTrace},
#ifdef __ZOWE_OS_LINUX
  /* TODO: move this somewhere else... no impact for z/OS Zowe currently. */
  {"DefaultCCSID", setFileInfoCCSID}, /* not a trace setting */
#endif

  {0,0}
};

#endif