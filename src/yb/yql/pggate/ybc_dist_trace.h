// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include "yb/yql/pggate/ybc_pg_typedefs.h"

/*
 * Constants for W3C traceparent parsing.
 *
 * A traceparent SQL comment looks like (without the backslash escapes):
 *   /\*traceparent='<version>-<traceid>-<parentid>-<sampled>'*\/
 * where:
 * version is 2 hex digits
 * traceid is 32 hex digits
 * parentid is 16 hex digits
 * sampled is 2 hex digits
 *
 * YB_TRACEPARENT_KEY_PREFIX      "traceparent="                    (12 chars)
 * YB_TRACEPARENT_VALUE_LEN       "00-...-01"                       (55 chars)
 * quote                          "'"                               ( 1 char )
 * SQL comment delimiters         "/ *"  or "* /"                   ( 2 chars)
 */
#define YB_TRACEPARENT_KEY_PREFIX                 "traceparent="
#define YB_TRACEPARENT_KEY_PREFIX_LEN             12
#define YB_TRACEPARENT_QUOTE_LEN                  1
#define YB_TRACEPARENT_COMMENT_DELIMITERS_LEN     2
#define YB_TRACEPARENT_VALUE_LEN                  55

#ifdef __cplusplus
extern "C" {
#endif

#define YB_DIST_TRACE_START_SPAN(op_name) \
  do { \
    if (YBCIsDistTraceActive()) { \
      YBCDistTraceStartSpan(op_name); \
    } \
  } while (0)

#define YB_DIST_TRACE_END_SPAN() \
  do { \
    if (YBCIsDistTraceActive()) { \
      YBCDistTraceEndSpan(); \
    } \
  } while (0)

bool YBCIsOtelScopeStackEmpty();
void YBCInitDistTrace(int64_t process_pid, const char* node_uuid);
void YBCCleanupDistTrace();
bool YBCIsDistTraceEnabled();
bool YBCIsDistTraceActive();
bool YBCIsTraceParentValidAndRemote(const char* traceparent);
YbcOtelSpanContext YBCGetValidSpanContext(const char* traceparent);
void YBCDestroySpanContext(YbcOtelSpanContext span_context);
void YBCDistTraceStartRootSpan(
    const char* query_string, YbcOtelSpanContext span_ctx, YbcPgOid db_oid, YbcPgOid user_id);
void YBCDistTraceStartSpan(const char* op_name);
void YBCDistTraceSetCurrSpanAttrUint64(const char* key, uint64_t value);
void YBCDistTraceSetCurrSpanAttrStr(const char* key, const char* value);
void YBCDistTraceEndSpan();
bool YBCDistTraceIsRootSpan();
void YBCDistTraceClearStack();

#ifdef __cplusplus
}  // extern "C"
#endif
