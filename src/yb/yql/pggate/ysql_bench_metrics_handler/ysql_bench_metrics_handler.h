// Copyright (c) YugaByte, Inc.
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

#pragma once

#ifdef __cplusplus
#include <atomic>
#else
#include <stdatomic.h>
#endif

#include "yb/yql/pggate/util/ybc_util.h"

#ifdef __cplusplus
namespace yb::pggate {
extern "C" {
#define YB_ATOMIC_ULLONG std::atomic_ullong
#else
#define YB_ATOMIC_ULLONG atomic_ullong
#endif

typedef struct ysqlBenchMetricEntry {
         YB_ATOMIC_ULLONG average_latency;
         YB_ATOMIC_ULLONG success_count;
         YB_ATOMIC_ULLONG failure_count;
         YB_ATOMIC_ULLONG latency_sum;
         YB_ATOMIC_ULLONG success_count_sum;
         YB_ATOMIC_ULLONG failure_count_sum;
} YsqlBenchMetricEntry;

struct WebserverWrapper;

struct WebserverWrapper *CreateWebserver(char *listen_addresses, int port);
int StartWebserver(struct WebserverWrapper *webserver);
void StopWebserver(struct WebserverWrapper *webserver);
void RegisterMetrics(YsqlBenchMetricEntry* ysql_bench_metric_entry, char *metric_node_name);
void InitGoogleLogging(char *prog_name);

#ifdef __cplusplus
}  // extern "C"
}  // namespace yb::pggate
#endif
