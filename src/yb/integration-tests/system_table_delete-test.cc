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

#include "yb/client/snapshot_test_util.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/mini_master.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::master {

using SystemTableDelete = pgwrapper::PgMiniTestBase;

// When creating a pg sys table with a hard coded table id, we need to ensure that delete and
// recreate of the table works.
TEST_F(SystemTableDelete, DeleteSysTableWithFixedOid) {
  const auto table_name = "my_special_sys_table";
  const auto create_table_stmt = Format(
      "CREATE TABLE pg_catalog.$0(messages bytea) WITH (table_oid = 8076, row_type_oid=8075)",
      table_name);

  // Setup PITR, so that we can ensure the table does not get HIDDEN when dropped on a failed
  // create.
  ASSERT_OK(
      client::SnapshotTestUtil(*cluster_.get(), client_->proxy_cache())
          .CreateSchedule("yugabyte", client::WaitSnapshot::kTrue));

  auto conn = ASSERT_RESULT(Connect());
  // Enable the connection to create pg system tables.
  ASSERT_OK(conn.Execute("SET ysql_upgrade_mode TO true"));

  // Force the table crete to fail.
  ASSERT_OK(conn.Execute("SET yb_test_fail_next_ddl TO 1"));
  ASSERT_NOK_STR_CONTAINS(conn.Execute(create_table_stmt), "Failed DDL operation as requested ");

  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();

  master::ListTablesRequestPB req;
  req.set_name_filter(table_name);
  req.set_include_not_running(true);

  ASSERT_OK(WaitFor(
      [&catalog_manager, &req]() -> Result<bool> {
        master::ListTablesResponsePB resp;
        RETURN_NOT_OK(catalog_manager.ListTables(&req, &resp));
        SCHECK_EQ(resp.tables_size(), 1, IllegalState, "Expected to find one table");
        LOG(INFO) << "Found table: " << resp.DebugString();
        return resp.tables(0).state() == SysTablesEntryPB::DELETED;
      },
      2min, "Waiting for table to be deleted"));

  ASSERT_OK(conn.Execute(create_table_stmt));

  ASSERT_OK(cluster_->StepDownMasterLeader());

  ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(1min));
}

} // namespace yb::master
