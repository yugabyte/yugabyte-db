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

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "gmock/gmock.h"
#include <gtest/gtest.h>

#include "yb/common/common_types.pb.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/ref_counted.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/clone/clone_state_entity.h"
#include "yb/master/clone/clone_state_manager.h"
#include "yb/master/clone/external_functions.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"

#include "yb/master/master_types.pb.h"
#include "yb/util/monotime.h"
#include "yb/util/pb_util.h"
#include "yb/util/physical_time.h"
#include "yb/util/status_format.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_db_clone);

namespace yb {
namespace master {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Mock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::SetArgPointee;

MATCHER_P(CloneTabletRequestPBMatcher, expected, "CloneTabletRequestPBs did not match") {
  return pb_util::ArePBsEqual(arg, expected, nullptr /* diff_str */);
}

// This is needed for the mock of GenerateSnapshotInfoFromSchedule.
std::ostream& operator<<(
    std::ostream& os, const Result<std::pair<SnapshotInfoPB, std::unordered_set<TabletId>>>& res) {
  if (!res.ok()) {
    os << res.status().ToString();
  } else {
    os << res->first.ShortDebugString();
    os << "Not snapshotted tablets: ";
    for (const auto& tablet_id : res->second) {
      os << tablet_id << ", ";
    }
  }
  return os;
}

class CloneStateManagerTest : public YBTest {
  class MockExternalFunctions : public CloneStateManagerExternalFunctionsBase {
   public:
    ~MockExternalFunctions() {}

    MOCK_METHOD(
        Status, ListSnapshotSchedules, (ListSnapshotSchedulesResponsePB* resp), (override));
    MOCK_METHOD(Status, DeleteSnapshot, (const TxnSnapshotId& snapshot_id), (override));
    MOCK_METHOD(
        Result<TxnSnapshotRestorationId>, Restore,
        (const TxnSnapshotId& snapshot_id, HybridTime restore_at), (override));
    MOCK_METHOD(
        Status, ListRestorations,
        (const TxnSnapshotRestorationId& restoration_id,
        ListSnapshotRestorationsResponsePB* resp), (override));

    MOCK_METHOD(Result<TabletInfoPtr>, GetTabletInfo, (const TabletId& tablet_id), (override));

    MOCK_METHOD(
        Result<NamespaceInfoPtr>, FindNamespace, (const NamespaceIdentifierPB&), (override));

    MOCK_METHOD(
        Status, ScheduleCloneTabletCall,
        (const TabletInfoPtr& source_tablet, LeaderEpoch epoch, tablet::CloneTabletRequestPB req),
        (override));
    MOCK_METHOD(
        Status, ScheduleClonePgSchemaTask,
        (const std::string& permanent_uuid, const std::string& source_db_name,
         const std::string& target_db_name, HybridTime restore_ht,
         AsyncClonePgSchema::ClonePgSchemaCallbackType callback, MonoTime deadline), (override));

    MOCK_METHOD(
        Status, Upsert, (const CloneStateInfoPtr& clone_state), (override));
    MOCK_METHOD(
        Status, Load,
        (const std::string& type,
         std::function<Status(const std::string&, const SysCloneStatePB&)> inserter), (override));

    MOCK_METHOD(
        Status, DoCreateSnapshot,
        (const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp,
         CoarseTimePoint deadline, const LeaderEpoch& epoch), (override));

    MOCK_METHOD(
        (Result<std::pair<SnapshotInfoPB, std::unordered_set<TabletId>>>),
        GenerateSnapshotInfoFromSchedule,
        (const SnapshotScheduleId& snapshot_schedule_id, HybridTime export_time,
        CoarseTimePoint deadline), (override));

    MOCK_METHOD(
        Status, DoImportSnapshotMeta,
        (const SnapshotInfoPB& snapshot_pb, const LeaderEpoch& epoch,
         const std::optional<std::string>& clone_target_namespace_name, NamespaceMap* namespace_map,
         UDTypeMap* type_map, ExternalTableSnapshotDataMap* tables_data,
         CoarseTimePoint deadline), (override));

    MOCK_METHOD(TSDescriptorPtr, PickTserver, (), (override));
  };

 private:
  std::string GetTestTabletId(bool source, int num) {
    return std::string(source ? "source_" : "target_") + "tablet_" + std::to_string(num);
  }

 protected:
  void SetUp() override {
    YBTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_db_clone) = true;
    clone_state_manager_ = std::unique_ptr<CloneStateManager>(
        new CloneStateManager(std::make_unique<MockExternalFunctions>()));

    // Set up namespaces.
    source_ns_ = make_scoped_refptr<NamespaceInfo>(kSourceNamespaceId, nullptr /* tasks_tracker */);
    {
      auto lock = source_ns_->LockForWrite();
      lock.mutable_data()->pb.set_database_type(kDatabaseType);
      lock.mutable_data()->pb.set_name(kSourceNamespaceName);
      lock.Commit();
    }

    target_ns_ = make_scoped_refptr<NamespaceInfo>(kTargetNamespaceId, nullptr /* tasks_tracker */);
    {
      auto lock = target_ns_->LockForWrite();
      lock.mutable_data()->pb.set_database_type(kDatabaseType);
      lock.mutable_data()->pb.set_name(kTargetNamespaceName);
      lock.Commit();
    }

    // Set up tables.
    source_table_ = make_scoped_refptr<TableInfo>(kSourceTableId, false /* colocated */);
    {
      auto lock = source_table_->LockForWrite();
      lock.mutable_data()->pb.set_namespace_id(kSourceNamespaceId);
      lock.mutable_data()->pb.set_namespace_name(kSourceNamespaceName);
      lock.Commit();
    }
    target_table_ = make_scoped_refptr<TableInfo>(kTargetTableId, false /* colocated */);
    {
      auto lock = target_table_->LockForWrite();
      lock.mutable_data()->pb.set_namespace_id(kTargetNamespaceId);
      lock.mutable_data()->pb.set_namespace_name(kTargetNamespaceName);
      lock.Commit();
    }

    // Set up tablets.
    for (int i = 0; i < kNumTablets; ++i) {
      auto source_tablet =
          make_scoped_refptr<TabletInfo>(source_table_, GetTestTabletId(true /* source */, i));
      auto target_tablet =
          make_scoped_refptr<TabletInfo>(target_table_, GetTestTabletId(false /* source */, i));

      source_tablets_.push_back(source_tablet);
      target_tablets_.push_back(target_tablet);
    }
  }

  // Creates a clone state in the CLONE_SCHEMA_STARTED state from a provided
  // ExternalTableSnapshotDataMap (instead of using import / export snapshot).
  // NB: This does not call EXPECT_CALL(Upsert) because some tests expect this to fail.
  Result<CloneStateInfoPtr> CreateCloneState(
      uint32_t seq_no, const ExternalTableSnapshotDataMap& table_snapshot_data) {
    auto clone_state = VERIFY_RESULT(clone_state_manager_->CreateCloneState(
        seq_no, kSourceNamespaceId, kTargetNamespaceName, kRestoreTime));

    RETURN_NOT_OK(clone_state_manager_->UpdateCloneStateWithSnapshotInfo(
        clone_state, kSourceSnapshotId, kTargetSnapshotId, table_snapshot_data));
    return clone_state;
  }

  Result<CloneStateInfoPtr> CreateSecondCloneState() {
    ExternalTableSnapshotDataMap table_snapshot_data;
    auto& table_data = table_snapshot_data[kSourceTableId];
    table_data.table_meta = ImportSnapshotMetaResponsePB::TableMetaPB();
    IdPairPB tablet_ids;
    tablet_ids.set_old_id("test_source_id");
    tablet_ids.set_new_id("test_target_id");
    *table_data.table_meta->add_tablets_ids() = tablet_ids;

    EXPECT_CALL(MockFuncs(), Upsert(_));
    return CreateCloneState(kSeqNo + 1, table_snapshot_data);
  }

  ExternalTableSnapshotDataMap DefaultTableSnapshotData() {
    ExternalTableSnapshotDataMap table_snapshot_data;
    auto& table_data = table_snapshot_data[kSourceTableId];
    table_data.table_meta = ImportSnapshotMetaResponsePB::TableMetaPB();
    for (int i = 0; i < kNumTablets; ++i) {
      IdPairPB tablet_ids;
      tablet_ids.set_old_id(source_tablets_[i]->id());
      tablet_ids.set_new_id(target_tablets_[i]->id());
      *table_data.table_meta->add_tablets_ids() = tablet_ids;
    }
    return table_snapshot_data;
  }

  // Creates a clone state and schedules clone ops to move it into the CREATING state.
  Result<CloneStateInfoPtr> CreateCloneStateAndStartCloning() {
    EXPECT_CALL(MockFuncs(), Upsert(_));
    auto clone_state = VERIFY_RESULT(CreateCloneState(kSeqNo, DefaultTableSnapshotData()));

    for (int i = 0; i < kNumTablets; ++i) {
      EXPECT_CALL(MockFuncs(), GetTabletInfo(source_tablets_[i]->id()))
          .WillOnce(Return(source_tablets_[i]));
      EXPECT_CALL(MockFuncs(), GetTabletInfo(target_tablets_[i]->id()))
          .WillOnce(Return(target_tablets_[i]));
      EXPECT_CALL(MockFuncs(), ScheduleCloneTabletCall(source_tablets_[i], kEpoch, _));
    }
    EXPECT_CALL(MockFuncs(), Upsert(_));
    RETURN_NOT_OK(ScheduleCloneOps(clone_state, kEpoch, {} /* not_snapshotted_tablets */));
    return clone_state;
  }

  Status HandleCreatingState(const CloneStateInfoPtr& clone_state) {
    return clone_state_manager_->HandleCreatingState(clone_state);
  }

  Status HandleRestoringState(const CloneStateInfoPtr& clone_state) {
    return clone_state_manager_->HandleRestoringState(clone_state);
  }

  Result<CloneStateInfoPtr> GetCloneStateFromSourceNamespace(const NamespaceId& namespace_id) {
    return clone_state_manager_->GetCloneStateFromSourceNamespace(namespace_id);
  }

  Status ScheduleCloneOps(
      const CloneStateInfoPtr& clone_state, const LeaderEpoch& epoch,
      const std::unordered_set<TabletId>& not_snapshotted_tablets) {
    return clone_state_manager_->ScheduleCloneOps(clone_state, epoch, not_snapshotted_tablets);
  }

  MockExternalFunctions& MockFuncs() {
    return static_cast<MockExternalFunctions&>(*clone_state_manager_->external_funcs_);
  }

  std::unique_ptr<CloneStateManager> clone_state_manager_;

  const uint32_t kSeqNo = 100;
  const NamespaceId kSourceNamespaceId = "source_namespace_id";
  const NamespaceId kTargetNamespaceId = "target_namespace_id";
  const std::string kSourceNamespaceName = "source_namespace_name";
  const std::string kTargetNamespaceName = "target_namespace_name";
  const SnapshotScheduleId kSnapshotScheduleId = SnapshotScheduleId::GenerateRandom();
  const TxnSnapshotId kSourceSnapshotId = TxnSnapshotId::GenerateRandom();
  const TxnSnapshotId kTargetSnapshotId = TxnSnapshotId::GenerateRandom();
  const TxnSnapshotRestorationId kRestorationId = TxnSnapshotRestorationId::GenerateRandom();
  const TableId kSourceTableId = "source_table_id";
  const TableId kTargetTableId = "target_table_id";
  const int kNumTablets = 2;
  const HybridTime kRestoreTime = HybridTime(12345);
  const YQLDatabase kDatabaseType = YQLDatabase::YQL_DATABASE_CQL;
  const LeaderEpoch kEpoch = LeaderEpoch(123 /* term */);

  NamespaceInfoPtr source_ns_;
  NamespaceInfoPtr target_ns_;
  TableInfoPtr source_table_;
  TableInfoPtr target_table_;
  std::vector<TabletInfoPtr> source_tablets_;
  std::vector<TabletInfoPtr> target_tablets_;
};

TEST_F(CloneStateManagerTest, CreateCloneState) {
  EXPECT_CALL(MockFuncs(), Upsert(_));
  auto clone_state = ASSERT_RESULT(CreateCloneState(kSeqNo, DefaultTableSnapshotData()));

  // Check clone state persisted fields.
  SysCloneStatePB expected_pb;
  expected_pb.set_aggregate_state(SysCloneStatePB::CLONE_SCHEMA_STARTED);
  expected_pb.set_source_namespace_id(kSourceNamespaceId);
  expected_pb.set_clone_request_seq_no(kSeqNo);
  expected_pb.set_target_namespace_name(kTargetNamespaceName);
  expected_pb.set_restore_time(kRestoreTime.ToUint64());
  std::string diff;
  bool same = pb_util::ArePBsEqual(clone_state->LockForRead()->pb, expected_pb, &diff);
  ASSERT_TRUE(same) << diff;

  // Check clone state manager in-memory fields.
  ASSERT_EQ(ASSERT_RESULT(
      GetCloneStateFromSourceNamespace(kSourceNamespaceId)), clone_state);
}

TEST_F(CloneStateManagerTest, CreateSecondCloneState) {
  // It should be possible to create a clone state if and only if the previous clone is in a
  // terminal state.
  ASSERT_RESULT(CreateCloneStateAndStartCloning());

  for (int i = SysCloneStatePB::State_MIN; i <= SysCloneStatePB::State_MAX; ++i) {
    auto state = SysCloneStatePB_State(i);
    auto current_clone_state = ASSERT_RESULT(GetCloneStateFromSourceNamespace(kSourceNamespaceId));

    LOG(INFO) << "Setting previous state to " << SysCloneStatePB::State_Name(state);
    auto l = current_clone_state->LockForWrite();
    l.mutable_data()->pb.set_aggregate_state(state);
    l.Commit();

    if (current_clone_state->LockForRead()->pb.aggregate_state() == SysCloneStatePB::RESTORED) {
      EXPECT_CALL(MockFuncs(), Upsert(_));
      ASSERT_OK(CreateCloneState(i + 1, DefaultTableSnapshotData()));
    } else {
      auto s = CreateCloneState(i + 1, DefaultTableSnapshotData());
      ASSERT_NOK(s);
      ASSERT_TRUE(s.status().IsAlreadyPresent());
    }
  }
}

TEST_F(CloneStateManagerTest, ScheduleCloneOps) {
  EXPECT_CALL(MockFuncs(), Upsert(_));
  auto clone_state = ASSERT_RESULT(CreateCloneState(kSeqNo, DefaultTableSnapshotData()));

  for (int i = 0; i < kNumTablets; ++i) {
    tablet::CloneTabletRequestPB expected_req;
    expected_req.set_tablet_id(source_tablets_[i]->id());
    expected_req.set_target_tablet_id(target_tablets_[i]->id());
    expected_req.set_source_snapshot_id(kSourceSnapshotId.data(), kSourceSnapshotId.size());
    expected_req.set_target_snapshot_id(kTargetSnapshotId.data(), kTargetSnapshotId.size());
    expected_req.set_target_table_id(kTargetTableId);
    expected_req.set_target_namespace_name(kTargetNamespaceName);
    expected_req.set_clone_request_seq_no(kSeqNo);
    expected_req.set_target_pg_table_id(target_table_->pg_table_id());
    *expected_req.mutable_target_schema() = target_table_->LockForRead()->schema();
    *expected_req.mutable_target_partition_schema() =
        target_table_->LockForRead()->pb.partition_schema();

    EXPECT_CALL(MockFuncs(), GetTabletInfo(source_tablets_[i]->id()))
        .WillOnce(Return(source_tablets_[i]));
    EXPECT_CALL(MockFuncs(), GetTabletInfo(target_tablets_[i]->id()))
        .WillOnce(Return(target_tablets_[i]));
    EXPECT_CALL(MockFuncs(), ScheduleCloneTabletCall(
        source_tablets_[i], kEpoch, CloneTabletRequestPBMatcher(expected_req)));
  }
  EXPECT_CALL(MockFuncs(), Upsert(_));
  ASSERT_OK(ScheduleCloneOps(clone_state, kEpoch, {} /* not_snapshotted_tablets */));
}

TEST_F(CloneStateManagerTest, HandleCreatingStateAllTabletsCreating) {
  auto clone_state = ASSERT_RESULT(CreateCloneStateAndStartCloning());

  for (int i = 0; i < kNumTablets; ++i) {
    EXPECT_CALL(MockFuncs(), GetTabletInfo(target_tablets_[i]->id()))
        .WillOnce(Return(target_tablets_[i]));
  }

  // Should not do anything.
  ASSERT_OK(HandleCreatingState(clone_state));

  // Aggregate state should still be CREATING.
  ASSERT_EQ(clone_state->LockForRead()->pb.aggregate_state(), SysCloneStatePB::CREATING);
}

TEST_F(CloneStateManagerTest, HandleCreatingStateSomeTabletsRunning) {
  ASSERT_GT(kNumTablets, 1);

  auto clone_state = ASSERT_RESULT(CreateCloneStateAndStartCloning());

  // Mark one tablet RUNNING.
  auto lock = target_tablets_[0]->LockForWrite();
  lock.mutable_data()->set_state(SysTabletsEntryPB::RUNNING, "Marked tablet 0 as running");
  lock.Commit();

  for (int i = 0; i < kNumTablets; ++i) {
    EXPECT_CALL(MockFuncs(), GetTabletInfo(target_tablets_[i]->id()))
        .WillOnce(Return(target_tablets_[i]));
  }
  ASSERT_OK(HandleCreatingState(clone_state));

  // Aggregate state should not transition to RESTORING yet.
  ASSERT_EQ(clone_state->LockForRead()->pb.aggregate_state(), SysCloneStatePB::CREATING);
}

TEST_F(CloneStateManagerTest, HandleCreatingStateAllTabletsRunning) {
  // Mark all tablets as RUNNING.
  for (int i = 0; i < kNumTablets; ++i) {
    auto lock = target_tablets_[i]->LockForWrite();
    lock.mutable_data()->set_state(
        SysTabletsEntryPB::RUNNING, Format("Marked tablet $0 as running", i));
    lock.Commit();
  }

  auto clone_state = ASSERT_RESULT(CreateCloneStateAndStartCloning());

  for (int i = 0; i < kNumTablets; ++i) {
    EXPECT_CALL(MockFuncs(), GetTabletInfo(target_tablets_[i]->id()))
        .WillOnce(Return(target_tablets_[i]));
  }

  // HandleCreatingState should transition aggregate state to RESTORING and should also trigger a
  // restore.
  EXPECT_CALL(MockFuncs(), Upsert(_));
  EXPECT_CALL(MockFuncs(), Restore(kTargetSnapshotId, kRestoreTime))
      .WillOnce(Return(kRestorationId));
  ASSERT_OK(HandleCreatingState(clone_state));

  auto read_lock  = clone_state->LockForRead();
  ASSERT_EQ(read_lock->pb.aggregate_state(), SysCloneStatePB::RESTORING);

  ASSERT_EQ(clone_state->RestorationId(), kRestorationId);
}

TEST_F(CloneStateManagerTest, HandleRestoringStateIncomplete) {
  auto clone_state = ASSERT_RESULT(CreateCloneStateAndStartCloning());
  {
    auto lock = clone_state->LockForWrite();
    lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::RESTORING);
    lock.Commit();
    clone_state->SetRestorationId(kRestorationId);
  }

  ListSnapshotRestorationsResponsePB resp;
  auto* restoration = resp.add_restorations();
  restoration->mutable_entry()->set_state(SysSnapshotEntryPB::RESTORING);
  EXPECT_CALL(MockFuncs(), ListRestorations(kRestorationId, _))
      .WillOnce(DoAll(SetArgPointee<1>(resp), Return(Status::OK())));

  // Should not do anything.
  ASSERT_OK(HandleRestoringState(clone_state));

  ASSERT_EQ(clone_state->LockForRead()->pb.aggregate_state(), SysCloneStatePB::RESTORING);
}

TEST_F(CloneStateManagerTest, HandleRestoringStateRestored) {
  auto clone_state = ASSERT_RESULT(CreateCloneStateAndStartCloning());
  {
    auto lock = clone_state->LockForWrite();
    lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::RESTORING);
    lock.Commit();
    clone_state->SetRestorationId(kRestorationId);
  }

  ListSnapshotRestorationsResponsePB resp;
  auto* restoration = resp.add_restorations();
  restoration->mutable_entry()->set_state(SysSnapshotEntryPB::RESTORED);
  EXPECT_CALL(MockFuncs(), ListRestorations(kRestorationId, _))
      .WillOnce(DoAll(SetArgPointee<1>(resp), Return(Status::OK())));
  EXPECT_CALL(MockFuncs(), Upsert);

  // Should transition the clone to the RESTORED state.
  ASSERT_OK(HandleRestoringState(clone_state));

  ASSERT_EQ(clone_state->LockForRead()->pb.aggregate_state(), SysCloneStatePB::RESTORED);
}

TEST_F(CloneStateManagerTest, Load) {
  auto clone_state = ASSERT_RESULT(CreateCloneStateAndStartCloning());

  // When the loader runs, we will save the function that is normally passed to
  // sys_catalog Load in 'inserter'.
  std::function<Status(const std::string&, const SysCloneStatePB&)> inserter;
  EXPECT_CALL(MockFuncs(), Load)
      .WillOnce(DoAll(SaveArg<1>(&inserter), Return(Status::OK())));
  ASSERT_OK(clone_state_manager_->ClearAndRunLoaders());

  // Run the inserter to actually load the data.
  ASSERT_OK(inserter(clone_state->id(), clone_state->LockForRead()->pb));

  auto loaded_clone_state = ASSERT_RESULT(GetCloneStateFromSourceNamespace(kSourceNamespaceId));

  // The loaded clone state should be a new object.
  ASSERT_NE(clone_state, loaded_clone_state);

  std::string diff;
  bool same = pb_util::ArePBsEqual(clone_state->LockForRead()->pb,
                                   loaded_clone_state->LockForRead()->pb, &diff);
  ASSERT_TRUE(same) << diff;
}

TEST_F(CloneStateManagerTest, LoadUsesLatestSeqNo) {
  // When there are multiple clone state infos, we should load the one with the latest seq_no.

  // Mark the clone state as RESTORED otherwise we will not be able to create the next clone state.
  auto clone_state1 = ASSERT_RESULT(CreateCloneStateAndStartCloning());
  {
    auto lock = clone_state1->LockForWrite();
    lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::RESTORED);
    lock.Commit();
  }

  auto clone_state2 = ASSERT_RESULT(CreateSecondCloneState());

  // Should load clone_state2 since it has a higher seq_no.
  {
    // When the loader runs, we will save the function that is normally passed to
    // sys_catalog Load in 'inserter'.
    std::function<Status(const std::string&, const SysCloneStatePB&)> inserter;
    EXPECT_CALL(MockFuncs(), Load)
        .WillRepeatedly(DoAll(SaveArg<1>(&inserter), Return(Status::OK())));
    ASSERT_OK(clone_state_manager_->ClearAndRunLoaders());

    // Run the inserter to actually load the data.
    ASSERT_OK(inserter(clone_state1->id(), clone_state1->LockForRead()->pb));
    ASSERT_OK(inserter(clone_state2->id(), clone_state2->LockForRead()->pb));

    // Should overwrite the first clone state.
    auto loaded_clone_state = ASSERT_RESULT(GetCloneStateFromSourceNamespace(kSourceNamespaceId));
    ASSERT_EQ(loaded_clone_state->LockForRead()->pb.clone_request_seq_no(),
              clone_state2->LockForRead()->pb.clone_request_seq_no());
  }

  // Same test as above but with the reversed sys catalog load order.
  {
    std::function<Status(const std::string&, const SysCloneStatePB&)> inserter;
    EXPECT_CALL(MockFuncs(), Load)
        .WillRepeatedly(DoAll(SaveArg<1>(&inserter), Return(Status::OK())));
    ASSERT_OK(clone_state_manager_->ClearAndRunLoaders());

    ASSERT_OK(inserter(clone_state2->id(), clone_state2->LockForRead()->pb));
    ASSERT_OK(inserter(clone_state1->id(), clone_state1->LockForRead()->pb));

    auto loaded_clone_state = ASSERT_RESULT(GetCloneStateFromSourceNamespace(kSourceNamespaceId));
    ASSERT_EQ(loaded_clone_state->LockForRead()->pb.clone_request_seq_no(),
              clone_state2->LockForRead()->pb.clone_request_seq_no());
  }
}

} // namespace master
} // namespace yb
