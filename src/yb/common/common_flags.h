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

#ifndef YB_COMMON_COMMON_FLAGS_H
#define YB_COMMON_COMMON_FLAGS_H

#include <gflags/gflags.h>

static constexpr int kAutoDetectNumShardsPerTServer = -1;

DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_bool(enable_ysql);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_bool(log_ysql_catalog_versions);
DECLARE_bool(TEST_enable_db_catalog_version_mode);

namespace yb {

// Performs the initialization of the common flags, as needed.
void InitCommonFlags();

} // namespace yb

#endif  // YB_COMMON_COMMON_FLAGS_H
