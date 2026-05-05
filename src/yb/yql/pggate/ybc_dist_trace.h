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

#ifdef __cplusplus
extern "C" {
#endif

void YBCInitDistTrace(int64_t process_pid, const char* node_uuid);
void YBCCleanupDistTrace();
bool YBCIsDistTraceEnabled();
YbcOtelScope YBCDistTraceStartRootSpan(
    const char* query_string, const char* traceparent, YbcPgOid db_oid, YbcPgOid user_id);
void YBCDistTraceSetSpanAttributeUint64(YbcOtelScope scope, const char* key, uint64_t value);
void YBCDistTraceEndSpan(YbcOtelScope scope);

#ifdef __cplusplus
}  // extern "C"
#endif
