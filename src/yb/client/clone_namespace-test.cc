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

#include "yb/client/client-test-util.h"
#include "yb/client/snapshot_schedule-test.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master.h"

#include "yb/util/backoff_waiter.h"

DECLARE_bool(enable_db_clone);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

namespace yb {
namespace client {

class CloneNamespaceTest : public SnapshotScheduleTest {
  void SetUp() override {
    SnapshotScheduleTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_db_clone) = true;
  }

 protected:
  Status CloneAndWait(
      const std::string& source_namespace_name, YQLDatabase source_namespace_type, HybridTime ht,
      const std::string& target_namespace_name) {
    rpc::RpcController controller;
    controller.set_timeout(60s);

    master::CloneNamespaceRequestPB clone_req;
    master::CloneNamespaceResponsePB clone_resp;

    // Set request fields.
    clone_req.mutable_source_namespace()->set_name(source_namespace_name);
    clone_req.mutable_source_namespace()->set_database_type(source_namespace_type);
    clone_req.set_restore_ht(ht.ToUint64());
    clone_req.set_target_namespace_name(target_namespace_name);

    auto backup_proxy = VERIFY_RESULT(snapshot_util_->MakeBackupServiceProxy());
    RETURN_NOT_OK(backup_proxy.CloneNamespace(clone_req, &clone_resp, &controller));
    if (clone_resp.has_error()) {
      return StatusFromPB(clone_resp.error().status());
    }

    // Wait until clone is done.
    master::ListClonesRequestPB done_req;
    master::ListClonesResponsePB done_resp;
    done_req.set_seq_no(clone_resp.seq_no());
    done_req.set_source_namespace_id(clone_resp.source_namespace_id());
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      controller.Reset();
      RETURN_NOT_OK(backup_proxy.ListClones(done_req, &done_resp, &controller));
      if (done_resp.has_error()) {
        return StatusFromPB(clone_resp.error().status());
      }
      RSTATUS_DCHECK(
          done_resp.entries_size() == 1, IllegalState,
          Format("Expected 1 clone entry, got $0", done_resp.entries_size()));
      auto state = done_resp.entries(0).aggregate_state();
      return state == master::SysCloneStatePB::ABORTED ||
             state == master::SysCloneStatePB::COMPLETE;
    }, 60s, "Wait for clone to finish"));
    return Status::OK();
  }
  const std::string kTargetNamespaceName = "clone_namespace";
};

TEST_F(CloneNamespaceTest, Clone) {
  const std::string kTargetNamespaceName2 = "clone_namespace2";
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, kTableName.namespace_type(),
                                   kTableName.namespace_name()));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));

  // Write two sets of rows.
  ASSERT_NO_FATALS(WriteData(WriteOpType::INSERT, 0 /* transaction */));
  auto row_count1 = CountTableRows(table_);
  auto ht1 = cluster_->mini_master()->master()->clock()->Now();
  ASSERT_NO_FATALS(WriteData(WriteOpType::INSERT, 1) /* transaction */);
  auto row_count2 = CountTableRows(table_);
  auto ht2 = cluster_->mini_master()->master()->clock()->Now();

  ASSERT_OK(CloneAndWait(
      kTableName.namespace_name(), YQLDatabase::YQL_DATABASE_CQL, ht1, kTargetNamespaceName));
  ASSERT_OK(CloneAndWait(
      kTableName.namespace_name(), YQLDatabase::YQL_DATABASE_CQL, ht2, kTargetNamespaceName2));

  // First clone should have only the first set of rows.
  YBTableName clone1(YQL_DATABASE_CQL, kTargetNamespaceName, kTableName.table_name());
  TableHandle clone1_handle;
  ASSERT_OK(clone1_handle.Open(clone1, client_.get()));
  ASSERT_EQ(CountTableRows(clone1_handle), row_count1);

  // Second clone should have all the rows.
  YBTableName clone2(YQL_DATABASE_CQL, kTargetNamespaceName2, kTableName.table_name());
  TableHandle clone2_handle;
  ASSERT_OK(clone2_handle.Open(clone2, client_.get()));
  ASSERT_EQ(CountTableRows(clone2_handle), row_count2);
}

TEST_F(CloneNamespaceTest, CloneWithNoSchedule) {
  // Write one row.
  ASSERT_NO_FATALS(WriteData(WriteOpType::INSERT, 0 /* transaction */));
  auto ht = cluster_->mini_master()->master()->clock()->Now();

  auto status = CloneAndWait(
      kTableName.namespace_name(), YQLDatabase::YQL_DATABASE_CQL, ht, kTargetNamespaceName);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "Could not find snapshot schedule");
}

TEST_F(CloneNamespaceTest, CloneAfterDrop) {
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, kTableName.namespace_type(),
                                   kTableName.namespace_name()));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));

  ASSERT_NO_FATALS(WriteData(WriteOpType::INSERT, 0 /* transaction */));
  auto ht = cluster_->mini_master()->master()->clock()->Now();

  ASSERT_OK(client_->DeleteTable(kTableName));

  ASSERT_OK(CloneAndWait(
      kTableName.namespace_name(), YQLDatabase::YQL_DATABASE_CQL, ht, kTargetNamespaceName));

  YBTableName clone(YQL_DATABASE_CQL, kTargetNamespaceName, kTableName.table_name());
  TableHandle clone_handle;
  ASSERT_OK(clone_handle.Open(clone, client_.get()));
  ASSERT_EQ(CountTableRows(clone_handle), kNumRows);
}

TEST_F(CloneNamespaceTest, DropClonedNamespace) {
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, kTableName.namespace_type(),
                                   kTableName.namespace_name()));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));
  auto ht = cluster_->mini_master()->master()->clock()->Now();

  ASSERT_OK(CloneAndWait(
      kTableName.namespace_name(), YQLDatabase::YQL_DATABASE_CQL, ht, kTargetNamespaceName));

  YBTableName clone(YQL_DATABASE_CQL, kTargetNamespaceName, kTableName.table_name());
  TableHandle clone_handle;
  ASSERT_OK(clone_handle.Open(clone, client_.get()));

  ASSERT_OK(client_->DeleteTable(clone));
  ASSERT_OK(client_->DeleteNamespace(kTargetNamespaceName));
}

TEST_F(CloneNamespaceTest, CloneFromOldestSnapshot) {
  // Check that we can use the oldest snapshot in a schedule to clone.
  const auto kInterval = 5s;
  const auto kRetention = 10s;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 100;

  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, kTableName.namespace_type(), kTableName.namespace_name(), kInterval, kRetention));

  auto first_snapshot = ASSERT_RESULT(snapshot_util_->WaitScheduleSnapshot(schedule_id));
  ASSERT_NO_FATALS(WriteData(WriteOpType::INSERT, 0 /* transaction */));
  auto ht = cluster_->mini_master()->master()->clock()->Now();

  // Wait for the first snapshot to be deleted and check that we can clone to a time between the
  // first and the second snapshot's hybrid times.
  ASSERT_OK(snapshot_util_->WaitSnapshotCleaned(TryFullyDecodeTxnSnapshotId(first_snapshot.id())));

  ASSERT_OK(CloneAndWait(
      kTableName.namespace_name(), YQLDatabase::YQL_DATABASE_CQL, ht, kTargetNamespaceName));

  YBTableName clone(YQL_DATABASE_CQL, kTargetNamespaceName, kTableName.table_name());
  TableHandle clone_handle;
  ASSERT_OK(clone_handle.Open(clone, client_.get()));
  ASSERT_EQ(CountTableRows(clone_handle), kNumRows);
}

} // namespace client
} // namespace yb
