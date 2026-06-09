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

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

#include "yb/client/yb_table_name.h"
#include "yb/common/snapshot.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/physical_time.h"
#include "yb/util/timestamp.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {

static constexpr std::string_view kYsqlUpgradeMsg{
    "YSQL upgrade was performed since the restore time"};
static constexpr std::string_view kYsqlMajorUpgradeMsg{
    "YSQL major catalog upgrade occurred between the restore time"};
static constexpr std::string_view kYsqlMajorUpgradeInProgressMsg{
    "Cannot clone database: YSQL major catalog upgrade is in progress"};
static constexpr std::string_view kYsqlMajorUpgradeAtRestoreTimeMsg{
    "YSQL major catalog upgrade was in state"};
static constexpr std::string_view kSysCatalogMissingTableMsg{
    "Sys catalog at restore state is missing a table"};
static constexpr std::string_view kSourceDbName{"source_db"};
static constexpr std::string_view kCloneDbName{"clone_db"};
static constexpr std::string_view kTestTableName{"test_table"};
static constexpr int kScheduleIntervalSec = 10;
static constexpr int kScheduleRetentionSec = 600;

class SnapshotUpgradeTest : public YsqlMajorUpgradeTestBase {
 public:
  SnapshotUpgradeTest() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
    AddUnDefOkAndSetFlag(opts.extra_master_flags, "enable_db_clone", "true");
  }

 protected:
  Result<SnapshotScheduleId> CreateSnapshotScheduleForDb(std::string_view db_name) {
    rpc::RpcController controller;
    controller.set_timeout(30s);
    master::CreateSnapshotScheduleRequestPB req;
    master::CreateSnapshotScheduleResponsePB resp;

    client::YBTableName keyspace;
    master::NamespaceIdentifierPB namespace_id;
    namespace_id.set_database_type(YQL_DATABASE_PGSQL);
    namespace_id.set_name(std::string{db_name});
    keyspace.GetFromNamespaceIdentifierPB(namespace_id);

    auto* options = req.mutable_options();
    auto* filter_tables = options->mutable_filter()->mutable_tables()->mutable_tables();
    keyspace.SetIntoTableIdentifierPB(filter_tables->Add());
    options->set_interval_sec(kScheduleIntervalSec);
    options->set_retention_duration_sec(kScheduleRetentionSec);

    auto backup_proxy = cluster_->GetLeaderMasterProxy<master::MasterBackupProxy>();
    RETURN_NOT_OK(backup_proxy.CreateSnapshotSchedule(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id());
  }

  Status RestoreSnapshotSchedule(
      const SnapshotScheduleId& schedule_id, const HybridTime& restore_ht) {
    rpc::RpcController controller;
    controller.set_timeout(60s * kTimeMultiplier);
    master::RestoreSnapshotScheduleRequestPB req;
    master::RestoreSnapshotScheduleResponsePB resp;
    req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
    req.set_restore_ht(restore_ht.ToUint64());
    auto backup_proxy = cluster_->GetLeaderMasterProxy<master::MasterBackupProxy>();
    RETURN_NOT_OK(backup_proxy.RestoreSnapshotSchedule(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status WaitForSnapshotInSchedule() {
    return WaitFor(
        [this]() -> Result<bool> {
          auto backup_proxy = cluster_->GetLeaderMasterProxy<master::MasterBackupProxy>();
          rpc::RpcController controller;
          controller.set_timeout(30s);
          master::ListSnapshotSchedulesRequestPB req;
          master::ListSnapshotSchedulesResponsePB resp;
          RETURN_NOT_OK(backup_proxy.ListSnapshotSchedules(req, &resp, &controller));
          if (resp.has_error()) {
            return false;
          }
          for (const auto& schedule : resp.schedules()) {
            if (schedule.snapshots_size() > 0) {
              const auto& snapshot = schedule.snapshots(0);
              if (snapshot.entry().state() == master::SysSnapshotEntryPB::COMPLETE) {
                return true;
              }
            }
          }
          return false;
        },
        60s * kTimeMultiplier, "Waiting for snapshot in schedule");
  }

  Status ValidateStatusContainsFailureMessageDueToRestore(Status s) {
    auto msg = s.message().ToBuffer();
    if (!msg.contains(kYsqlUpgradeMsg) && !msg.contains(kSysCatalogMissingTableMsg)) {
      return STATUS_FORMAT(
          IllegalState, "Expected error message to contain '$0' or '$1', was instead: $2",
          kYsqlUpgradeMsg, kSysCatalogMissingTableMsg, msg);
    }
    return Status::OK();
  }

  Status CloneDB(
      pgwrapper::PGConn& conn, std::string_view source_db, std::string_view clone_db,
      MicrosTime as_of) {
    return conn.ExecuteFormat(
        "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", clone_db, source_db, as_of);
  }
};

// A YSQL major upgrade should block clone in multiple ways.
//   1. after a YSQL major upgrade has begun and before it is finalized we should not clone
//   2. after a YSQL major upgrade is finalized, we should not clone as of before the upgrade
//        began
//   3. after a YSQL major upgrade is finalized, we should not clone as of after the upgrade
//        began and before the upgrade was finalized
TEST_F(SnapshotUpgradeTest, YSQLMajorUpgradeBlocksClone) {
  auto yb_conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(yb_conn.ExecuteFormat("CREATE DATABASE $0", kSourceDbName));
  auto source_conn = ASSERT_RESULT(cluster_->ConnectToDB(kSourceDbName));
  ASSERT_OK(
      source_conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTestTableName));
  ASSERT_OK(source_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i FROM generate_series(1, 10) i", kTestTableName));

  ASSERT_OK(CreateSnapshotScheduleForDb(kSourceDbName));
  ASSERT_OK(WaitForSnapshotInSchedule());

  auto pre_upgrade_time = ASSERT_RESULT(WallClock()->Now()).time_point;

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, false));

  auto pre_finalize_time = ASSERT_RESULT(WallClock()->Now()).time_point;

  // (1) do not allow clones while an upgrade is in progress.
  {
    yb_conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto status = CloneDB(yb_conn, kSourceDbName, kCloneDbName, pre_upgrade_time);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), kYsqlMajorUpgradeInProgressMsg);
  }

  ASSERT_OK(FinalizeUpgrade());

  auto post_upgrade_time = ASSERT_RESULT(WallClock()->Now()).time_point;

  // (2) Do not allow clones to before the upgrade began.
  {
    yb_conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto status = CloneDB(yb_conn, kSourceDbName, kCloneDbName, pre_upgrade_time);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), kYsqlMajorUpgradeMsg);
  }

  // (3) Do not allow clones to after the upgrade began and before it was finalized.
  {
    yb_conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto status = CloneDB(yb_conn, kSourceDbName, kCloneDbName, pre_finalize_time);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), kYsqlMajorUpgradeAtRestoreTimeMsg);
  }

  source_conn = ASSERT_RESULT(cluster_->ConnectToDB(kSourceDbName));
  ASSERT_OK(source_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i FROM generate_series(11, 20) i", kTestTableName));
  ASSERT_OK(source_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kCloneDbName, kSourceDbName, post_upgrade_time));
  auto cloned_db_conn = ASSERT_RESULT(cluster_->ConnectToDB(kCloneDbName));
  auto count = ASSERT_RESULT(
      cloned_db_conn.FetchRowAsString(Format("SELECT count(*) FROM $0", kTestTableName)));
  ASSERT_EQ(count, "10");
}

// PITR (RestoreSnapshotSchedule) to a time before a major upgrade should fail because the
// pg_yb_migration row count changes during the upgrade.
TEST_F(SnapshotUpgradeTest, PITRToPreUpgradeTimeFails) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kSourceDbName));
  conn = ASSERT_RESULT(cluster_->ConnectToDB(kSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTestTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i FROM generate_series(1, 10) i", kTestTableName));

  auto schedule_id = ASSERT_RESULT(CreateSnapshotScheduleForDb(kSourceDbName));
  ASSERT_OK(WaitForSnapshotInSchedule());

  Timestamp pre_upgrade_time(ASSERT_RESULT(WallClock()->Now()).time_point);
  auto restore_ht = ASSERT_RESULT(HybridTime::ParseHybridTime(pre_upgrade_time.ToString()));

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  auto status = RestoreSnapshotSchedule(schedule_id, restore_ht);
  ASSERT_NOK(status);
  ASSERT_OK(ValidateStatusContainsFailureMessageDueToRestore(status));

  // Verify the database is still usable after the failed restore attempt: reads, writes, and DDL
  // should all succeed.
  conn = ASSERT_RESULT(cluster_->ConnectToDB(kSourceDbName));
  auto count = ASSERT_RESULT(conn.FetchRowAsString(
      Format("SELECT count(*) FROM $0", kTestTableName)));
  ASSERT_EQ(count, "10");

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (11, 11)", kTestTableName));
  count = ASSERT_RESULT(conn.FetchRowAsString(
      Format("SELECT count(*) FROM $0", kTestTableName)));
  ASSERT_EQ(count, "11");

  ASSERT_OK(conn.Execute("CREATE TABLE post_restore_table (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO post_restore_table VALUES (1)"));
  auto post_restore_val = ASSERT_RESULT(conn.FetchRowAsString(
      "SELECT id FROM post_restore_table"));
  ASSERT_EQ(post_restore_val, "1");
}

}  // namespace yb
