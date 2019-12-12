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

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

#include "yb/util/size_literals.h"
#include "yb/util/format.h"

using namespace yb::size_literals;

DECLARE_int64(retryable_rpc_single_call_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

namespace yb {
namespace pgwrapper {

void PgWrapperTestBase::SetUp() {
  YBMiniClusterTestBase::SetUp();

  ExternalMiniClusterOptions opts;
  opts.enable_ysql = true;

  // TODO Increase the rpc timeout (from 2500) to not time out for long master queries (i.e. for
  // Postgres system tables). Should be removed once the long lock issue is fixed.
  const int kSingleCallTimeoutMs = NonTsanVsTsan(10000, 30000);
  const string rpc_flag_str =
      "--retryable_rpc_single_call_timeout_ms=" + std::to_string(kSingleCallTimeoutMs);
  opts.extra_master_flags.emplace_back(rpc_flag_str);

  if (IsTsan()) {
    // Increase timeout for admin ops to account for create database with copying during initdb.
    // This will be useful when we start enabling PostgreSQL tests under TSAN.
    opts.extra_master_flags.emplace_back(
        "--yb_client_admin_operation_timeout_sec=120");
  }

  opts.extra_tserver_flags.emplace_back(rpc_flag_str);

  // With ysql_num_shards_per_tserver=1 and 3 tservers we'll be creating 3 tablets per table, which
  // is enough for most tests.
  opts.extra_tserver_flags.emplace_back("--ysql_num_shards_per_tserver=1");

  // Collect old records very aggressively to catch bugs with old readpoints.
  opts.extra_tserver_flags.emplace_back("--timestamp_history_retention_interval_sec=0");

  opts.extra_master_flags.emplace_back("--hide_pg_catalog_table_creation_logs");

  opts.num_masters = GetNumMasters();

  opts.num_tablet_servers = GetNumTabletServers();

  opts.extra_master_flags.emplace_back("--client_read_write_timeout_ms=120000");
  opts.extra_master_flags.emplace_back(Format("--memory_limit_hard_bytes=$0", 2_GB));

  UpdateMiniClusterOptions(&opts);

  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());

  if (cluster_->num_tablet_servers() > 0) {
    pg_ts = cluster_->tablet_server(0);
  }

  // TODO: fix cluster verification for PostgreSQL tables.
  DontVerifyClusterBeforeNextTearDown();
}

} // namespace pgwrapper
} // namespace yb
