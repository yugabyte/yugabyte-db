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
//

#include "yb/common/common_flags.h"

#include <thread>

#include "yb/util/atomic.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/tsan_util.h"
#include "yb/gutil/sysinfo.h"

// Note that this is used by the client or master only, not by tserver.
DEFINE_int32(yb_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines the number of tablets "
    "based on number of CPU cores.");

DEFINE_int32(ysql_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per YSQL table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines the number of tablets "
    "based on number of CPU cores.");

DEFINE_bool(ysql_disable_index_backfill, false,
    "A kill switch to disable multi-stage backfill for YSQL indexes.");
TAG_FLAG(ysql_disable_index_backfill, hidden);
TAG_FLAG(ysql_disable_index_backfill, advanced);

DEFINE_bool(enable_pg_savepoints, false,
            "True to enable savepoints in YugaByte PostgreSQL API. This should eventually be set "
            "to true by default.");
TAG_FLAG(enable_pg_savepoints, unsafe);

namespace yb {

static int GetYCQLNumShardsPerTServer() {
  int value = 8;
  if (IsTsan()) {
    value = 2;
  } else if (base::NumCPUs() <= 2) {
    value = 4;
  }
  return value;
}

static int GetYSQLNumShardsPerTServer() {
  int value = 8;
  if (IsTsan()) {
    value = 2;
  } else if (base::NumCPUs() <= 2) {
    value = 2;
  } else if (base::NumCPUs() <= 4) {
    value = 4;
  }
  return value;
}

void InitCommonFlags() {
  if (GetAtomicFlag(&FLAGS_yb_num_shards_per_tserver) == kAutoDetectNumShardsPerTServer) {
    int value = GetYCQLNumShardsPerTServer();
    VLOG(1) << "Auto setting FLAGS_yb_num_shards_per_tserver to " << value;
    SetAtomicFlag(value, &FLAGS_yb_num_shards_per_tserver);
  }
  if (GetAtomicFlag(&FLAGS_ysql_num_shards_per_tserver) == kAutoDetectNumShardsPerTServer) {
    int value = GetYSQLNumShardsPerTServer();
    VLOG(1) << "Auto setting FLAGS_ysql_num_shards_per_tserver to " << value;
    SetAtomicFlag(value, &FLAGS_ysql_num_shards_per_tserver);
  }
}

} // namespace yb
