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

#pragma once

#include "yb/util/flags.h"

static constexpr int kAutoDetectNumShardsPerTServer = -1;

DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_bool(enable_ysql);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_bool(log_ysql_catalog_versions);
DECLARE_bool(ysql_enable_db_catalog_version_mode);
DECLARE_bool(ysql_enable_pg_per_database_oid_allocator);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_bool(ysql_yb_enable_replication_slot_consumption);
DECLARE_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms);
DECLARE_bool(TEST_ysql_hide_catalog_version_increment_log);
DECLARE_int32(ysql_clone_pg_schema_rpc_timeout_ms);

namespace yb {

// Performs the initialization of the common flags, as needed.
void InitCommonFlags();

} // namespace yb
