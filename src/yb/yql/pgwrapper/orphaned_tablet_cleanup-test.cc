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


#include <string.h>

#include <memory>
#include <thread>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/rpc/rpc_context.h"

#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_initialization.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(master_enable_universe_uuid_heartbeat_check);
DECLARE_bool(TEST_disable_tablet_deletion);
DECLARE_bool(master_enable_deletion_check_for_orphaned_tablets);
DECLARE_bool(TEST_simulate_sys_catalog_data_loss);
DECLARE_bool(TEST_create_snapshot_schedule_with_null_rpc_context);
DECLARE_int32(pgsql_proxy_webserver_port);

using std::string;

namespace yb {
namespace pgwrapper {

// Testing that the DB does not lose data in the event of orchestration or configuration errors.
class OrphanedTabletCleanupTest : public PgMiniTestBase {
 public:
  Status SetUpWithParams(bool create_snapshot_schedule_on_database = false) {
    if (create_snapshot_schedule_on_database) {
      RETURN_NOT_OK(CreateSnapshotScheduleOnDatabase(kDatabaseName));
      SleepFor(MonoDelta::FromSeconds(kWaitTimeForSnapshotScheduleSetupCompleteSec));
    }

    auto db_conn = VERIFY_RESULT(ConnectToDB(kDatabaseName));
    RETURN_NOT_OK(db_conn.ExecuteFormat(
        "CREATE TABLE $0 (i int primary key) SPLIT INTO 1 TABLETS", kTableName));
    return db_conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (generate_series(0, $1))", kTableName, kNumValuesToWrite - 1);
  }

  Status CreateSnapshotScheduleOnDatabase(const string& database_name) {
    master::GetNamespaceInfoResponsePB resp;
    RETURN_NOT_OK(client_->GetNamespaceInfo(
        "" /* namespace_id */, kDatabaseName, YQL_DATABASE_PGSQL, &resp));
    auto namespace_id = resp.namespace_().id();

    auto keyspace = client::YBTableName();
    keyspace.set_namespace_id(namespace_id);
    keyspace.set_namespace_name(database_name);

    master::CreateSnapshotScheduleRequestPB req;
    auto& options = *req.mutable_options();
    auto& filter_tables = *options.mutable_filter()->mutable_tables()->mutable_tables();
    keyspace.SetIntoTableIdentifierPB(filter_tables.Add());
    options.set_interval_sec(60);
    options.set_retention_duration_sec(600);
    master::CreateSnapshotScheduleResponsePB snapshot_resp;
    auto* leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto master_backup_proxy = std::make_unique<master::MasterBackupProxy>(
        &client_->proxy_cache(), leader_master->bound_rpc_addr());
    rpc::RpcController rpc;
    RETURN_NOT_OK(master_backup_proxy->CreateSnapshotSchedule(req, &snapshot_resp, &rpc));
    SCHECK(!snapshot_resp.has_error(), IllegalState, "CreateSnapshotScheduleResponse has error");
    return Status::OK();
  }

  void VerifyTabletDataOnTserver(size_t num_expected_tablet_peers) {
    // Ensure that the number of tablet peers present on tservers matches the expected count. As
    // more tests are added to this class, we can make this a stronger condition.
    auto tablet_peers = ListTabletPeers(
        cluster_.get(), ListPeersFilter::kAll, IncludeTransactionStatusTablets::kFalse);
    ASSERT_EQ(tablet_peers.size(), num_expected_tablet_peers);
  }

  const string kDatabaseName = "yugabyte";
  const string kTableName = "foo";
  const int kNumValuesToWrite = 10;
  // The amount of time tests wait for at least one round of master tserver heartbeats to go
  // through and the subsequent DeleteTablets to be issued.
  const int kWaitTimeForDeleteTabletProcessingSec = 20;
  const int kWaitTimeForSnapshotScheduleSetupCompleteSec = 10;
};

TEST_F(OrphanedTabletCleanupTest, SysCatalogCorruption) {
  ASSERT_OK(SetUpWithParams());
  // Ensure that if sys catalog is wiped, that orphaned tablet path is not triggered.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_sys_catalog_data_loss) = true;

  // Step down the leader to trigger a full tablet report and validate that we do not lose any data.
  auto new_leader = ASSERT_RESULT(cluster_->StepDownMasterLeader());
  SleepFor(MonoDelta::FromSeconds(kWaitTimeForDeleteTabletProcessingSec));
  ASSERT_NO_FATALS(VerifyTabletDataOnTserver(3 /* num_expected_tablet_peers */));
}

TEST_F(OrphanedTabletCleanupTest, PreviousBehaviorWithNoDeletionCheck) {
  ASSERT_OK(SetUpWithParams());
  // Test with master_enable_deletion_check_for_orphaned_tablets disabled that if sys catalog is
  // wiped, that tablets that are thought to be orphaned are cleaned up.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_enable_deletion_check_for_orphaned_tablets) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_sys_catalog_data_loss) = true;
  auto new_leader = ASSERT_RESULT(cluster_->StepDownMasterLeader());

  SleepFor(MonoDelta::FromSeconds(kWaitTimeForDeleteTabletProcessingSec));
  ASSERT_NO_FATALS(VerifyTabletDataOnTserver(0 /* num_expected_tablet_peers */));
}

TEST_F(OrphanedTabletCleanupTest, MasterLeaderFailover) {
  ASSERT_OK(SetUpWithParams());
  // Make sure that actually orphaned tablets are successfully deleted after a master leader
  // failover.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_tablet_deletion) = true;
  auto db_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(db_conn.ExecuteFormat("DROP TABLE $0", kTableName));
  auto new_leader = ASSERT_RESULT(cluster_->StepDownMasterLeader());
  ASSERT_NO_FATALS(VerifyTabletDataOnTserver(3));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_tablet_deletion) = false;

  SleepFor(MonoDelta::FromSeconds(kWaitTimeForDeleteTabletProcessingSec));
  ASSERT_NO_FATALS(VerifyTabletDataOnTserver(0 /* num_expected_tablet_peers */));
}

TEST_F(OrphanedTabletCleanupTest, HiddenTabletsNotDeletedAfterCorruption) {
  // Make sure that hidden tablets do not get erroneously deleted if sys catalog is corrupted.
  // We do this by setting up a snapshot schedule on the database.
  ASSERT_OK(SetUpWithParams(true /* create_snapshot_schedule_on_database */));
  auto db_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(db_conn.ExecuteFormat("DROP TABLE $0", kTableName));
  ASSERT_NO_FATALS(VerifyTabletDataOnTserver(3 /* num_expected_tablet_peers */));

  auto leader_master = ASSERT_RESULT(cluster_->StepDownMasterLeader());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_sys_catalog_data_loss) = true;

  SleepFor(MonoDelta::FromSeconds(kWaitTimeForDeleteTabletProcessingSec));
  ASSERT_NO_FATALS(VerifyTabletDataOnTserver(3 /* num_expected_tablet_peers */));
}

} // namespace pgwrapper
} // namespace yb
