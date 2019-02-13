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

DECLARE_int64(retryable_rpc_single_call_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

namespace yb {
namespace pgwrapper {

void PgWrapperTestBase::SetUp() {
  YBMiniClusterTestBase::SetUp();

  ExternalMiniClusterOptions opts;
  opts.start_pgsql_proxy = true;

  // TODO Increase the rpc timeout (from 2500) to not time out for long master queries (i.e. for
  // Postgres system tables). Should be removed once the long lock issue is fixed.
  int rpc_timeout = NonTsanVsTsan(10000, 30000);
  string rpc_flag = "--retryable_rpc_single_call_timeout_ms=";
  opts.extra_tserver_flags.emplace_back(rpc_flag + std::to_string(rpc_timeout));

  // With 3 tservers we'll be creating 3 tables per table, which is enough.
  opts.extra_tserver_flags.emplace_back("--yb_num_shards_per_tserver=1");
  opts.extra_tserver_flags.emplace_back("--pg_transactions_enabled");

  // Collect old records very aggressively to catch bugs with old readpoints.
  opts.extra_tserver_flags.emplace_back("--timestamp_history_retention_interval_sec=0");

  opts.extra_master_flags.emplace_back("--hide_pg_catalog_table_creation_logs");

  FLAGS_retryable_rpc_single_call_timeout_ms = rpc_timeout; // needed by cluster-wide initdb

  if (IsTsan()) {
    // Increase timeout for admin ops to account for create database with copying during initdb
    FLAGS_yb_client_admin_operation_timeout_sec = 120;
  }

  // Test that we can start PostgreSQL servers on non-colliding ports within each tablet server.
  opts.num_tablet_servers = 3;

  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());

  pg_ts = cluster_->tablet_server(0);

  // TODO: fix cluster verification for PostgreSQL tables.
  DontVerifyClusterBeforeNextTearDown();
}

} // namespace pgwrapper
} // namespace yb
