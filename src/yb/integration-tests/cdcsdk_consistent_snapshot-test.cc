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

#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

#include "yb/cdc/cdc_state_table.h"

namespace yb {
namespace cdc {

class CDCSDKConsistentSnapshotTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;

    // Disable pg replication command support to ensure that consistent snapshot feature
    // works independently.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_yb_enable_replication_commands) = false;

  }

  void TestCSStreamFailureRollback(std::string sync_point, std::string expected_error);
};


TEST_F(CDCSDKConsistentSnapshotTest, TestCSStreamSnapshotEstablishment) {
  // Disable running UpdatePeersAndMetrics for this test
  FLAGS_enable_log_retention_by_op_idx = false;
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));

  // Create a Consistent Snapshot Stream with NOEXPORT_SNAPSHOT option
  xrepl::StreamId stream1_id =
      ASSERT_RESULT(CreateConsistentSnapshotStream(CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT));
  ASSERT_NOK(GetSnapshotDetailsFromCdcStateTable(
      stream1_id, tablet_peer->tablet_id(), test_client()));
  auto checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream1_id, tablet_peer->tablet_id()));

  // NOEXPORT_SNAPSHOT option - Check for the following
  // 1. snapshot_key must be null
  // 2. Checkpoint should be X.Y where X, Y > 0
  // 3. History cutoff time should be HybridTime::kInvalid
  // 4. Checkpoint index Y (in X.Y) >= cdc_min_replicated_index
  // 5. cdc_sdk_min_checkpoint_op_id.index = cdc_min_replicated_index
  LogRetentionBarrierAndRelatedDetails(checkpoint_result, tablet_peer);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().term(), 0);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().index(), 0);
  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_GE(checkpoint_result.checkpoint().op_id().index(),
            tablet_peer->get_cdc_min_replicated_index());
  ASSERT_EQ(tablet_peer->cdc_sdk_min_checkpoint_op_id().index,
            tablet_peer->get_cdc_min_replicated_index());

  // Create a Consistent Snapshot Stream with USE_SNAPSHOT option
  xrepl::StreamId stream2_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  const auto& snapshot_time_key_pair =
      ASSERT_RESULT(GetSnapshotDetailsFromCdcStateTable(
          stream2_id, tablet_peer->tablet_id(), test_client()));
  checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream2_id, tablet_peer->tablet_id()));

  // USE_SNAPSHOT option - Check for the following
  // 1. snapshot_key must not be null
  // 2. Checkpoint should be X.Y where X, Y > 0
  // 3. snapshot_time > history cut off time
  // 4. Checkpoint index Y (in X.Y) >= cdc_min_replicated_index
  // 5. cdc_sdk_min_checkpoint_op_id.index = cdc_min_replicated_index
  LogRetentionBarrierAndRelatedDetails(checkpoint_result, tablet_peer);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().term(), 0);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().index(), 0);
  ASSERT_GT(std::get<0>(snapshot_time_key_pair), tablet_peer->get_cdc_sdk_safe_time().ToUint64());
  ASSERT_GE(checkpoint_result.checkpoint().op_id().index(),
            tablet_peer->get_cdc_min_replicated_index());
  ASSERT_EQ(tablet_peer->cdc_sdk_min_checkpoint_op_id().index,
            tablet_peer->get_cdc_min_replicated_index());
}

void CDCSDKConsistentSnapshotTest::TestCSStreamFailureRollback(
    std::string sync_point, std::string expected_error) {
  // Make UpdatePeersAndMetrics and Catalog Manager background tasks run frequently.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = 100;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));

  std::atomic<bool> force_failure = true;
  yb::SyncPoint::GetInstance()->SetCallBack(sync_point, [&force_failure, &sync_point](void* arg) {
    LOG(INFO) << "CDC stream creation sync point callback: " << sync_point
              << ", force_failure: " << force_failure;
    auto should_fail = reinterpret_cast<bool*>(arg);
    *should_fail = force_failure;
  });
  SyncPoint::GetInstance()->EnableProcessing();

  auto s = CreateConsistentSnapshotStream();
  ASSERT_NOK(s);
  if (sync_point == "CreateCDCSDKStream::kWhileStoringConsistentSnapshotDetails") {
    ASSERT_NE(s.status().message().AsStringView().find("CreateCDCStream RPC"), std::string::npos)
        << s.status().message().AsStringView();
    ASSERT_NE(s.status().message().AsStringView().find("timed out after"), std::string::npos)
        << s.status().message().AsStringView();
  } else {
    ASSERT_NE(s.status().message().AsStringView().find(expected_error), std::string::npos)
        << s.status().message().AsStringView();
  }
  LOG(INFO) << "Asserted the stream creation failures";

  // Allow the background UpdatePeersAndMetrics to clean up the stream.
  SleepFor(
      MonoDelta::FromSeconds(4 * FLAGS_update_min_cdc_indices_interval_secs * kTimeMultiplier));

  LOG(INFO) << "Checking the list of DB streams.";
  auto list_streams_resp = ASSERT_RESULT(ListDBStreams());
  ASSERT_EQ(list_streams_resp.streams_size(), 0) << list_streams_resp.DebugString();

  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);

  // Future stream creations must succeed. Disable running UpdatePeersAndMetrics now so that it
  // doesn't interfere with the safe time.
  force_failure = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_log_retention_by_op_idx) = false;

  LOG(INFO) << "Creating Consistent snapshot stream again.";
  xrepl::StreamId stream1_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  const auto& snapshot_time_key_pair =
      ASSERT_RESULT(GetSnapshotDetailsFromCdcStateTable(
          stream1_id, tablet_peer->tablet_id(), test_client()));
  auto checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream1_id, tablet_peer->tablet_id()));

  LogRetentionBarrierAndRelatedDetails(checkpoint_result, tablet_peer);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().term(), 0);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().index(), 0);
  ASSERT_GT(std::get<0>(snapshot_time_key_pair), tablet_peer->get_cdc_sdk_safe_time().ToUint64());
  ASSERT_LE(checkpoint_result.checkpoint().op_id().index(),
            tablet_peer->get_cdc_min_replicated_index());
  ASSERT_EQ(tablet_peer->cdc_sdk_min_checkpoint_op_id().index,
            tablet_peer->get_cdc_min_replicated_index());

  list_streams_resp = ASSERT_RESULT(ListDBStreams());
  ASSERT_EQ(list_streams_resp.streams_size(), 1);

  yb::SyncPoint::GetInstance()->DisableProcessing();
  yb::SyncPoint::GetInstance()->ClearAllCallBacks();
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCSStreamFailureRollbackFailureBeforeSysCatalogEntry) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kBeforeSysCatalogEntry",
      "Test failure for sync point CreateCDCSDKStream::kBeforeSysCatalogEntry.");
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCSStreamFailureRollbackFailureBeforeInMemoryCommit) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kBeforeInMemoryStateCommit",
      "Test failure for sync point CreateCDCSDKStream::kBeforeInMemoryStateCommit.");
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCSStreamFailureRollbackFailureAfterInMemoryStateCommit) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kAfterInMemoryStateCommit",
      "Test failure for sync point CreateCDCSDKStream::kAfterInMemoryStateCommit.");
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCSStreamFailureRollbackFailureAfterDummy) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kAfterDummyCDCStateEntries",
      "Test failure for sync point CreateCDCSDKStream::kAfterDummyCDCStateEntries.");
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCSStreamFailureRollbackFailureAfterRetentionBarriers) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kAfterRetentionBarriers",
      "Test failure for sync point CreateCDCSDKStream::kAfterRetentionBarriers.");
}

TEST_F(
    CDCSDKConsistentSnapshotTest,
    TestCSStreamFailureRollbackFailureWhileStoringConsistentSnapshot) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kWhileStoringConsistentSnapshotDetails", "" /* ignored */);
}

TEST_F(
    CDCSDKConsistentSnapshotTest,
    TestCSStreamFailureRollbackFailureAfterStoringConsistentSnapshot) {
  TestCSStreamFailureRollback(
      "CreateCDCSDKStream::kAfterStoringConsistentSnapshotDetails",
      "Test failure for sync point CreateCDCSDKStream::kAfterStoringConsistentSnapshotDetails.");
}

// The goal of this test is to confirm that the retention barriers are set
// for the slowest consumer
TEST_F(CDCSDKConsistentSnapshotTest, TestTwoCSStream) {
  // Disable running UpdatePeersAndMetrics for this test
  FLAGS_enable_log_retention_by_op_idx = false;
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));

  auto get_tablet_state = [tablet_peer]() {
    return std::tuple{tablet_peer->get_cdc_min_replicated_index(),
                      tablet_peer->cdc_sdk_min_checkpoint_op_id(),
                      tablet_peer->get_cdc_sdk_safe_time()};
  };

  // Create a Consistent Snapshot Stream with USE_SNAPSHOT option
  ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto state1 = get_tablet_state();

  // Create a second Consistent Snapshot Stream with USE_SNAPSHOT option
  ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto state2 = get_tablet_state();

  //  Check that all the barriers are for the slowest consumer
  //  which would be stream1 as it was created before stream 2
  ASSERT_EQ(state1, state2);
}

// The goal of this test is to check if the cdc_state table entries are populated
// as expected even if the ALTER TABLE from any tablet is slow
TEST_F(CDCSDKConsistentSnapshotTest, TestCreateStreamWithSlowAlterTable) {
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto table = EXPECT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 2));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);

  yb::SyncPoint::GetInstance()->SetCallBack("AsyncAlterTable::CDCSDKCreateStream", [&](void* arg) {
    auto sync_point_tablet_id = reinterpret_cast<std::string*>(arg);
    LOG(INFO) << "Tablet id: " << *sync_point_tablet_id;
    if (*sync_point_tablet_id == tablets[0].tablet_id()) {
      LOG(INFO) << "Found the tablet for which we shall slow down AlterTable";
      SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));
    }
  });
  SyncPoint::GetInstance()->EnableProcessing();

  // Create a Consistent Snapshot Stream with USE_SNAPSHOT option
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  auto checkpoints = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));
  for (auto cp : checkpoints) {
    LOG(INFO) << "Checkpoint = " << cp;
    ASSERT_NE(cp, OpId::Invalid());
  }

  yb::SyncPoint::GetInstance()->DisableProcessing();
  yb::SyncPoint::GetInstance()->ClearAllCallBacks();
}

// The goal of this test is to confirm that the consistent snapshot related metadata is
// persisted in the sys_catalog
TEST_F(CDCSDKConsistentSnapshotTest, TestConsistentSnapshotMetadataPersistence) {
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));

  // Create a Consistent Snapshot Stream with USE_SNAPSHOT option
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // Restart the universe
  test_cluster()->Shutdown();
  ASSERT_OK(test_cluster()->StartSync());

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_TRUE(resp.stream().has_cdcsdk_consistent_snapshot_option());
  ASSERT_TRUE(resp.stream().has_cdcsdk_consistent_snapshot_time());
  ASSERT_TRUE(resp.stream().has_stream_creation_time());
  for (const auto& option : resp.stream().options()) {
    if (option.key() == "state") {
      master::SysCDCStreamEntryPB_State state;
      ASSERT_TRUE(
          master::SysCDCStreamEntryPB_State_Parse(option.value(), &state));
      ASSERT_EQ(state, master::SysCDCStreamEntryPB_State::SysCDCStreamEntryPB_State_ACTIVE);
    }
  }
}

// Test related to race between UpdatePeersAndMetrics and stream creation in
// setting retention barriers
TEST_F(CDCSDKConsistentSnapshotTest, TestRetentionBarrierSettingRace) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 10;
  google::SetVLOGLevel("tablet*", 1);
  SyncPoint::GetInstance()->LoadDependency(
      {{"Tablet::SetAllInitialCDCSDKRetentionBarriers::End", "UpdatePeersAndMetrics::Start"},
       {"UpdateTabletPeersWithMaxCheckpoint::Done",
        "PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails::Start"}});
  SyncPoint::GetInstance()->EnableProcessing();

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));
  auto stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_TRUE(DeleteCDCStream(stream_id));
  // Create a Consistent Snapshot Stream with USE_SNAPSHOT option
  auto stream1_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // Check that UpdatePeersAndMetrics has been blocked from releasing retention barriers
  auto checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream1_id, tablet_peer->tablet_id()));
  LogRetentionBarrierAndRelatedDetails(checkpoint_result, tablet_peer);
  ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_LT(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  ASSERT_LT(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());

  // Now, drop the consistent snapshot stream and check that retention barriers are released
  ASSERT_TRUE(DeleteCDCStream(stream1_id));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 1;
  VerifyTransactionParticipant(tablet_peer->tablet_id(), OpId::Max());
  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_EQ(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
}

// Insert a row before snapshot. Insert a row after snapshot.
// Expected records: (DDL, READ) and (DDL, INSERT).
TEST_F(CDCSDKConsistentSnapshotTest, InsertBeforeAfterSnapshot) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);

  // This should be part of the snapshot
  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // This should be considered a change
  ASSERT_OK(WriteRows(2 /* start */, 3 /* end */, &test_cluster_));

  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 0, 0, 1, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records_before_snapshot[] = {{0, 0}, {1, 2}};
  ExpectedRecord expected_records_after_snapshot[] = {{2, 3}};

  GetChangesResponsePB change_resp_updated =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));

  uint32_t expected_record_count = 0;
  for (const auto& record : change_resp_updated.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecord(record, expected_records_before_snapshot[expected_record_count++], count);
  }

  GetChangesResponsePB change_resp_after_snapshot =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp_updated));

  expected_record_count = 0;
  for (const auto& record : change_resp_after_snapshot.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecord(record, expected_records_after_snapshot[expected_record_count++], count);
  }
  CheckCount(expected_count, count);
}

// Begin transaction, insert one row, commit transaction, enable snapshot
// Expected records: (DDL, READ).
TEST_F(CDCSDKConsistentSnapshotTest, InsertSingleRowSnapshot) {
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(3, 1, false));

  ASSERT_OK(WriteRowsHelper(1 /* start */, 2 /* end */, &test_cluster_, true));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 0, 0, 0, 1, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}};

  ConsumeSnapshotAndVerifyRecords(
      stream_id, tablets, cp_resp, expected_records, expected_count, count);
}

// Begin transaction, insert one row, commit transaction, update, enable snapshot
// Expected records: (DDL, READ).
TEST_F(CDCSDKConsistentSnapshotTest, UpdateInsertedRowSnapshot) {
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(3, 1, false));

  ASSERT_OK(WriteRowsHelper(1 /* start */, 2 /* end */, &test_cluster_, true));
  ASSERT_OK(UpdateRows(1 /* key */, 1 /* value */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 0, 0, 0, 1, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 1}};

  ConsumeSnapshotAndVerifyRecords(
      stream_id, tablets, cp_resp, expected_records, expected_count, count);
}

// Begin transaction, insert one row, commit transaction, delete, enable snapshot
// Expected records: (DDL).
TEST_F(CDCSDKConsistentSnapshotTest, DeleteInsertedRowSnapshot) {
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(3, 1, false));

  ASSERT_OK(WriteRowsHelper(1 /* start */, 2 /* end */, &test_cluster_, true));
  ASSERT_OK(DeleteRows(1 /* key */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 0, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}};

  ConsumeSnapshotAndVerifyRecords(
      stream_id, tablets, cp_resp, expected_records, expected_count, count);
}

// Insert 10K rows using a thread and after a while enable snapshot.
// Expected sum of READs and INSERTs is 10K.
TEST_F(CDCSDKConsistentSnapshotTest, InsertBeforeDuringSnapshot) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);

  // 10K records inserted using a thread.
  std::vector<std::thread> threads;
  threads.emplace_back(
      [&]() { ASSERT_OK(WriteRows(1 /* start */, 10001 /* end */, &test_cluster_)); });

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // Count the number of snapshot READs.
  GetChangesResponsePB change_resp;
  uint32_t reads_snapshot =
      ASSERT_RESULT(ConsumeSnapshotAndVerifyCounts(stream_id, tablets, cp_resp, &change_resp));

  for (auto& t : threads) {
    t.join();
  }

  LOG(INFO) << "Insertion of records using threads has completed.";

  // Count the number of INSERTS.
  uint32_t inserts_snapshot =
      ASSERT_RESULT(ConsumeInsertsAndVerifyCounts(stream_id, tablets, change_resp));

  LOG(INFO) << "Got " << reads_snapshot << " snapshot (read) records";
  LOG(INFO) << "Got " << inserts_snapshot << " insert records";
  LOG(INFO) << "Got " << reads_snapshot + inserts_snapshot << " total (read + insert) record";
  ASSERT_EQ(reads_snapshot + inserts_snapshot, 10000);
}

// Insert 10K rows using a thread and after a while enable snapshot.
// After snapshot completes, insert 10K rows using threads.
// Expected sum of READs and INSERTs is 20K.
TEST_F(CDCSDKConsistentSnapshotTest, InsertBeforeDuringAfterSnapshot) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);

  // 10K records inserted using a thread.
  std::vector<std::thread> threads;
  threads.emplace_back(
      [&]() { ASSERT_OK(WriteRows(1 /* start */, 10001 /* end */, &test_cluster_)); });

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // Count the number of snapshot READs.
  GetChangesResponsePB change_resp;
  uint32_t reads_snapshot =
      ASSERT_RESULT(ConsumeSnapshotAndVerifyCounts(stream_id, tablets, cp_resp, &change_resp));

  // Two threads used to insert records after the snapshot is over.
  threads.emplace_back(
      [&]() { ASSERT_OK(WriteRows(10001 /* start */, 15001 /* end */, &test_cluster_)); });
  threads.emplace_back(
      [&]() { ASSERT_OK(WriteRows(15001 /* start */, 20001 /* end */, &test_cluster_)); });

  for (auto& t : threads) {
    t.join();
  }

  LOG(INFO) << "Insertion of records using threads has completed.";

  // Count the number of INSERTS.
  uint32_t inserts_snapshot =
      ASSERT_RESULT(ConsumeInsertsAndVerifyCounts(stream_id, tablets, change_resp));

  LOG(INFO) << "Got " << reads_snapshot << " snapshot (read) records";
  LOG(INFO) << "Got " << inserts_snapshot << " insert records";
  LOG(INFO) << "Got " << reads_snapshot + inserts_snapshot << " total (read + insert) record";
  ASSERT_EQ(reads_snapshot + inserts_snapshot, 20000);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestSnapshotWithInvalidFromOpId) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  cp_resp.set_index(-1);
  cp_resp.set_term(-1);

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
    change_resp.mutable_cdc_sdk_checkpoint()->set_index(-1);
    change_resp.mutable_cdc_sdk_checkpoint()->set_term(-1);
    if (reads_snapshot == 1000) {
      break;
    }
  }
  ASSERT_EQ(reads_snapshot, 1000);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestMultipleTableAlterWithSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);

  // Table having key:value_1 column
  ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_));
  // Add column value_2 column, Table Alter happen.
  ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
  ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName));
  ASSERT_OK(WriteRows(
      101 /* start */, 201 /* end */, &test_cluster_, {kValue2ColumnName, kValue3ColumnName}));

  // Drop value_2 column, Tablet Alter happen.
  ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
  ASSERT_OK(WriteRows(201 /* start */, 301 /* end */, &test_cluster_, {kValue3ColumnName}));

  // Add the 2 columns, value_2 and value_4
  ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue4ColumnName));
  ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
  ASSERT_OK(WriteRows(
      301 /* start */, 401 /* end */, &test_cluster_,
      {kValue2ColumnName, kValue3ColumnName, kValue4ColumnName}));

  ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue2ColumnName));
  ASSERT_OK(DropColumn(&test_cluster_, kNamespaceName, kTableName, kValue3ColumnName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  std::vector<std::string> expected_columns{kKeyColumnName, kValueColumnName, kValue4ColumnName};
  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    if (record_size == 0) {
      break;
    }
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      std::vector<std::string> actual_columns;
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      std::stringstream s;
      if (record.row_message().op() == RowMessage::READ) {
        for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
          s << " " << record.row_message().new_tuple(jdx).datum_int32();
          actual_columns.push_back(record.row_message().new_tuple(jdx).column_name());
        }
        ASSERT_EQ(expected_columns, actual_columns);
        LOG(INFO) << "row: " << i << " : " << s.str();
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
  }
  ASSERT_EQ(reads_snapshot, 400);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestLeadershipChangeDuringSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  bool do_change_leader = true;
  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    if (record_size == 0) {
      break;
    }

    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;

    if (do_change_leader) {
      size_t first_leader_index = -1;
      size_t first_follower_index = -1;
      GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
      if (first_leader_index == 0) {
        // We want to avoid the scenario where the first TServer is the leader, since we want to
        // shut the leader TServer down and call GetChanges. GetChanges will be called on the
        // cdc_proxy based on the first TServer's address and we want to avoid the network issues.
        ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
        std::swap(first_leader_index, first_follower_index);
      }
      ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));
      SleepFor(MonoDelta::FromSeconds(10));
      do_change_leader = false;
    }
  }
  ASSERT_EQ(reads_snapshot, 1000);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestServerFailureDuringSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_single_record_update) = false;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(3, 1, false));

  // Table having key:value_1 column
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  bool do_snapshot_failure = false;

  auto result = UpdateCheckpoint(stream_id, tablets, cp_resp);
  while (true) {
    if (first_read) {
      first_read = false;
    } else {
      result = UpdateCheckpoint(stream_id, tablets, &change_resp);
    }

    if (!result.ok()) {
      ASSERT_EQ(FLAGS_TEST_cdc_snapshot_failure, true);
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_snapshot_failure) = false;
      LOG(INFO) << "Snapshot operation is failed retry again....";
      continue;
    }
    GetChangesResponsePB change_resp_updated = *result;
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    if (record_size == 0) {
      break;
    }
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      std::stringstream s;

      if (record.row_message().op() == RowMessage::READ) {
        for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
          s << " " << record.row_message().new_tuple(jdx).datum_int32();
        }
        LOG(INFO) << "row: " << i << " : " << s.str();
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;

    if (!do_snapshot_failure) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_snapshot_failure) = true;
      do_snapshot_failure = true;
    }
  }
  ASSERT_EQ(reads_snapshot, 200);
}

TEST_F(CDCSDKConsistentSnapshotTest, InsertedRowInbetweenSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(3, 1, false));

  ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  ASSERT_OK(WriteRows(101 /* start */, 201 /* end */, &test_cluster_));

  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  int count = 0;
  uint32_t record_size = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();

    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        count += 1;
      }
    }
    change_resp = change_resp_updated;
    if (change_resp_updated.cdc_sdk_checkpoint().key().empty() &&
        change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
    SleepFor(MonoDelta::FromSeconds(2));
  }
  ASSERT_EQ(count, 100);

  // Read the cdc_state table veriy that checkpoint set is non-zero
  auto checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));
  ASSERT_GT(checkpoint_result.checkpoint().op_id().term(), 0);
  ASSERT_GT(checkpoint_result.checkpoint().op_id().index(), 0);

  count = 0;
  change_resp = ASSERT_RESULT(
      GetChangesFromCDC(stream_id, tablets, &change_resp_updated.cdc_sdk_checkpoint()));
  record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == RowMessage::INSERT) {
      count += 1;
    }
  }
  ASSERT_EQ(count, 100);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestStreamActiveWithSnapshot) {
  // This testcase is to verify during snapshot operation, active time needs to be updated in
  // cdc_state table, so that stream should not expire if the snapshot operation takes longer than
  // the stream expiry time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 20000;  // 20 seconds

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));

  // Inserting 1000 rows, so that there will be 100 snapshot batches each with
  // 'FLAGS_cdc_snapshot_batch_size'(10) rows.
  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
                                                CDCSDKSnapshotOption::USE_SNAPSHOT,
                                                CDCCheckpointType::IMPLICIT));
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  int count = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  // There will be atleast 100 calls to 'GetChanges', and we wait 1 second between each iteration.
  // If the active time wasn't updated during the process, 'GetChanges' would fail before we get all
  // data.
  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();

    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        count += 1;
      }
    }
    change_resp = change_resp_updated;
    if (change_resp_updated.cdc_sdk_checkpoint().key().empty() &&
        change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
    LOG(INFO) << "Number of snapshot records read so far: " << count;
    SleepFor(MonoDelta::FromSeconds(1));
  }
  // We assert we got all the data after 100 iterations , which means the stream was active even
  // after ~100 seconds.
  ASSERT_EQ(count, 1000);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCheckpointUpdatedDuringSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
                                                CDCSDKSnapshotOption::USE_SNAPSHOT,
                                                CDCCheckpointType::IMPLICIT));
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  int count = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;

  uint64_t last_seen_snapshot_safe_time = 0;
  std::string last_seen_snapshot_key = "";

  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();

    const auto& snapshopt_time_key_pair = ASSERT_RESULT(GetSnapshotDetailsFromCdcStateTable(
        stream_id, tablets.begin()->tablet_id(), test_client()));

    auto const& checkpoint_result =
        ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

    // Assert that 'GetCDCCheckpoint' return the same snapshot_time and key as in 'cdc_state' table.
    ASSERT_EQ(checkpoint_result.snapshot_time(), std::get<0>(snapshopt_time_key_pair));
    ASSERT_EQ(checkpoint_result.snapshot_key(), std::get<1>(snapshopt_time_key_pair));

    if (last_seen_snapshot_safe_time != 0) {
      // Assert that the snapshot safe time does not change per 'GetChanges' call.
      ASSERT_EQ(last_seen_snapshot_safe_time, std::get<0>(snapshopt_time_key_pair));
    }
    last_seen_snapshot_safe_time = std::get<0>(snapshopt_time_key_pair);
    ASSERT_NE(last_seen_snapshot_safe_time, 0);

    if (!last_seen_snapshot_key.empty()) {
      // Assert that the snapshot key is updated per 'GetChanges' call.
      LOG(INFO) << "Comparing snapshot keys to establish change";
      ASSERT_NE(last_seen_snapshot_key, std::get<1>(snapshopt_time_key_pair));
    }
    last_seen_snapshot_key = std::get<1>(snapshopt_time_key_pair);

    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        count += 1;
      }
    }
    change_resp = change_resp_updated;
    if (change_resp_updated.cdc_sdk_checkpoint().key().empty() &&
        change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
  }
  ASSERT_EQ(count, 1000);

  // Call GetChanges after snapshot done. We should no loner see snapshot key and snasphot save_time
  // in cdc_state table.
  change_resp_updated = ASSERT_RESULT(UpdateSnapshotDone(stream_id, tablets));

  // We should no longer be able to get the snapshot key and safe_time from 'cdc_state' table.
  ASSERT_NOK(
      GetSnapshotDetailsFromCdcStateTable(stream_id, tablets.begin()->tablet_id(), test_client()));

  auto const& checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));
  ASSERT_EQ(checkpoint_result.snapshot_time(), 0);
  ASSERT_FALSE(checkpoint_result.has_snapshot_key());
}

TEST_F(CDCSDKConsistentSnapshotTest, TestSnapshotNoData) {
  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // We are calling 'GetChanges' in snapshot mode, but sine there is no data in the tablet, the
  // first response itself should indicate the end of snapshot.
  GetChangesResponsePB change_resp = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
  // 'write_id' must be set to 0, 'key' must to empty, to indicate that the snapshot is done.
  ASSERT_EQ(change_resp.cdc_sdk_checkpoint().write_id(), 0);
  ASSERT_EQ(change_resp.cdc_sdk_checkpoint().key(), "");

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));

  change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  ASSERT_GT(change_resp.cdc_sdk_proto_records_size(), 1000);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestSnapshotForColocatedTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_update_local_peer_min_index) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, true /* colocated */));

  // ASSERT_OK(CreateColocatedObjects(&test_cluster_));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test1(id1 int primary key, value_2 int, value_3 int);"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test2(id2 int primary key, value_2 int, value_3 int, "
                         "value_4 int);"));

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  const int64_t snapshot_recrods_per_table = 500;
  for (int i = 0; i < snapshot_recrods_per_table; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1, $2)", i, i + 1, i + 2));
    ASSERT_OK(
        conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1, $2, $3)", i, i + 1, i + 2, i + 3));
  }

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // Assert that we get all records from the first table: "test1".
  auto req_table_id = GetColocatedTableId("test1");
  ASSERT_NE(req_table_id, "");
  auto cp_resp =
      ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), req_table_id));
  VerifySnapshotOnColocatedTables(
      stream_id, tablets, cp_resp, req_table_id, "test1", snapshot_recrods_per_table);
  LOG(INFO) << "Verified snapshot records for table: test1";

  // Assert that we get all records from the second table: "test2".
  req_table_id = GetColocatedTableId("test2");
  ASSERT_NE(req_table_id, "");
  cp_resp =
      ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), req_table_id));
  VerifySnapshotOnColocatedTables(
      stream_id, tablets, cp_resp, req_table_id, "test2", snapshot_recrods_per_table);
  LOG(INFO) << "Verified snapshot records for table: test2";
}

TEST_F(CDCSDKConsistentSnapshotTest, TestCommitTimeRecordTimeAndNoSafepointRecordForSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));

  // Commit a transaction with 1000 rows.
  ASSERT_OK(WriteRowsHelper(1 /* start */, 1001 /* end */, &test_cluster_, true));

  // Insert 1000 single shard transactions
  ASSERT_OK(WriteRows(1001 /* start */, 2001 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  int count = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;
  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    uint64_t expected_commit_time = 0;
    uint64_t expected_record_time = 0;
    bool seen_safepoint_record = false;
    for (const auto& record : change_resp_updated.cdc_sdk_proto_records()) {
      if (record.row_message().op() == RowMessage::READ) {
        if (expected_commit_time == 0 && expected_record_time == 0) {
          expected_commit_time = record.row_message().commit_time();
          expected_record_time = record.row_message().record_time();
        } else {
          ASSERT_EQ(record.row_message().commit_time(), expected_commit_time);
          ASSERT_EQ(record.row_message().record_time(), expected_record_time);
        }

        count += 1;
      } else if (record.row_message().op() == RowMessage::SAFEPOINT) {
        seen_safepoint_record = true;
      }
    }
    ASSERT_EQ(seen_safepoint_record, false);

    change_resp = change_resp_updated;
    if (change_resp_updated.cdc_sdk_checkpoint().key().empty() &&
        change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
  }
  ASSERT_EQ(count, 2000);
}

TEST_F(CDCSDKConsistentSnapshotTest, TestGetCheckpointOnAddedColocatedTableWithNoSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_update_local_peer_min_index) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, true /* colocated */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test1(id1 int primary key, value_2 int, value_3 int);"));

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  const int64_t snapshot_recrods_per_table = 100;
  for (int i = 0; i < snapshot_recrods_per_table; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1, $2)", i, i + 1, i + 2));
  }

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  auto req_table_id = GetColocatedTableId("test1");
  ASSERT_NE(req_table_id, "");
  auto cp_resp =
    ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), req_table_id));

  bool first_call = true;
  GetChangesResponsePB change_resp;
  while (true) {
    if (first_call) {
      change_resp =
          ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp, req_table_id));
      first_call = false;
    } else {
      change_resp =
          ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp, req_table_id));
    }

    if (change_resp.cdc_sdk_checkpoint().key().empty() &&
        change_resp.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
  }
  ASSERT_RESULT(UpdateSnapshotDone(stream_id, tablets, req_table_id));
  LOG(INFO) << "Streamed snapshot records for table: test1";

  for (int i = snapshot_recrods_per_table; i < 2 * snapshot_recrods_per_table; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1, $2)", i, i + 1, i + 2));
  }

  auto stream_change_resp_before_add_table =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp, req_table_id));
  stream_change_resp_before_add_table = ASSERT_RESULT(
      UpdateCheckpoint(stream_id, tablets, &stream_change_resp_before_add_table, req_table_id));

  auto streaming_checkpoint_resp =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), req_table_id));
  ASSERT_FALSE(streaming_checkpoint_resp.has_snapshot_key());

  // Wait until the newly added table is added to the stream's metadata.
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test2(id1 int primary key, value_2 int, value_3 int);"));
  auto added_table_id = GetColocatedTableId("test2");
  ASSERT_OK(WaitFor(
      [&]() {
        auto result = GetCDCStreamTableIds(stream_id);
        if (!result.ok()) {
          return false;
        }
        const auto& table_ids = result.get();
        return std::find(table_ids.begin(), table_ids.end(), added_table_id) != table_ids.end();
      },
      MonoDelta::FromSeconds(kRpcTimeout), "New table not added to stream"));

  auto added_table_checkpoint_resp =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), added_table_id));
  ASSERT_EQ(OpId::FromPB(added_table_checkpoint_resp.checkpoint().op_id()), OpId::Invalid());

  ASSERT_RESULT(UpdateSnapshotDone(stream_id, tablets, added_table_id));
  added_table_checkpoint_resp =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id(), added_table_id));
  ASSERT_EQ(
      OpId::FromPB(streaming_checkpoint_resp.checkpoint().op_id()),
      OpId::FromPB(added_table_checkpoint_resp.checkpoint().op_id()));
}

TEST_F(CDCSDKConsistentSnapshotTest, TestSnapshotRecordSnapshotKey) {
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  FLAGS_cdc_snapshot_batch_size = 10;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  int count = 0;
  bool first_read = true;
  GetChangesResponsePB change_resp;
  GetChangesResponsePB change_resp_updated;

  while (true) {
    if (first_read) {
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, cp_resp));
      first_read = false;
    } else {
      change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    }

    for (int32_t i = 0; i < change_resp_updated.cdc_sdk_proto_records_size(); ++i) {
      const CDCSDKProtoRecordPB& record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        count += 1;
        ASSERT_EQ(record.cdc_sdk_op_id().term(), cp_resp.term());
        ASSERT_EQ(record.cdc_sdk_op_id().index(), cp_resp.index());
        ASSERT_EQ(record.cdc_sdk_op_id().write_id_key(), cp_resp.key());
      }
    }
    cp_resp = change_resp_updated.cdc_sdk_checkpoint();
    change_resp = change_resp_updated;
    if (change_resp_updated.cdc_sdk_checkpoint().key().empty() &&
        change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
  }
  ASSERT_EQ(count, 1000);
}

// Test Consistent Snapshot across 2 tables
//  1) Create a non CS stream and demonstrate non consistent snapshot
//  2) Create a CS stream and demonstrate consistent snapshot
TEST_F(CDCSDKConsistentSnapshotTest, TestConsistentSnapshotAcrossMultipleTables) {
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test1(id1 int primary key);"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test2(id2 int primary key);"));

  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets1;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &tablets1, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets1.size(), 1);

  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets2;
  ASSERT_OK(test_client()->GetTablets(table2, 0, &tablets2, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets2.size(), 1);

  // Insert a row each into test1 and test2
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0)", 1));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0)", 1));

  // Create a Non Consistent Snapshot Stream and another Consistent Snapshot stream
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  xrepl::StreamId cs_stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // Setup snapshot boundary on test1 for stream_id
  auto resp1 = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets1));
  ASSERT_FALSE(resp1.has_error());
  GetChangesResponsePB change_resp1 = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets1));

  // CS stream - GetCheckpoint for test1
  auto cp_resp1 =
    ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(cs_stream_id, tablets1[0].tablet_id()));

  // Insert a 2nd row into both tables
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0)", 2));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0)", 2));

  // Setup snapshot boundary on test2 for stream_id
  auto resp2 = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets2));
  ASSERT_FALSE(resp2.has_error());
  GetChangesResponsePB change_resp2 = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets2));

  // CS stream - GetCheckpoint for test2
  auto cp_resp2 =
    ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(cs_stream_id, tablets2[0].tablet_id()));

  // Now get the snapshot records for table 1 and verify that you get only 1 row (+1 DDL record)
  GetChangesResponsePB change_resp_updated1 =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets1, &change_resp1));
  uint32_t expected_record_count = 0;
  for (const auto& record : change_resp_updated1.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    expected_record_count++;
  }
  ASSERT_EQ(expected_record_count, 2);

  // Now get the snapshot records for table 2 and verify that you get 2 rows (+1 DDL record)
  GetChangesResponsePB change_resp_updated2 =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets2, &change_resp2));
  expected_record_count = 0;
  for (const auto& record : change_resp_updated2.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    expected_record_count++;
  }
  ASSERT_EQ(expected_record_count, 3);

  // Consistent Snapshot Stream
  // The number of snapshot records for both tables should be 1 READ record and 1 DDL record
  GetChangesResponsePB change_resp_updated_cs1 =
      ASSERT_RESULT(UpdateCheckpoint(cs_stream_id, tablets1, cp_resp1));
  expected_record_count = 0;
  for (const auto& record : change_resp_updated_cs1.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    expected_record_count++;
  }
  ASSERT_EQ(expected_record_count, 2);

  GetChangesResponsePB change_resp_updated_cs2 =
      ASSERT_RESULT(UpdateCheckpoint(cs_stream_id, tablets2, cp_resp2));
  expected_record_count = 0;
  for (const auto& record : change_resp_updated_cs2.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    expected_record_count++;
  }
  ASSERT_EQ(expected_record_count, 2);

}

// Test to ensure that retention barriers on tables for which there is no interest in
// consumption are released. This should apply only to consistent snapshot streams
// and not to older version streams.
TEST_F(CDCSDKConsistentSnapshotTest, TestReleaseResourcesOnUnpolledTablets) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_tablet_not_of_interest_timeout_secs) = 3;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test1(id1 int primary key);"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test2(id2 int primary key);"));

  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets1;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &tablets1, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets1.size(), 1);
  auto tablet1_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets1.begin()->tablet_id()));

  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets2;
  ASSERT_OK(test_client()->GetTablets(table2, 0, &tablets2, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets2.size(), 1);
  auto tablet2_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets2.begin()->tablet_id()));

  // Insert a row each into test1 and test2
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0)", 1));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0)", 1));

  // Create a Non Consistent Snapshot Stream and another Consistent Snapshot streams
  auto stream_id = ASSERT_RESULT(CreateDBStream());
  auto cs_stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // For cs_stream_id, only poll table1 but not table2
  auto cp_resp1 =
      ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(cs_stream_id, tablets1[0].tablet_id()));
  auto cp_resp2 =
      ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(cs_stream_id, tablets2[0].tablet_id()));
  auto change_resp1 = ASSERT_RESULT(UpdateCheckpoint(cs_stream_id, tablets1, cp_resp1));

  SleepFor(MonoDelta::FromSeconds(4));
  ASSERT_NOK(UpdateCheckpoint(cs_stream_id, tablets2, cp_resp2));
  ASSERT_OK(UpdateCheckpoint(cs_stream_id, tablets1, &change_resp1));
  ASSERT_OK(GetLastActiveTimeFromCdcStateTable(stream_id, tablets1[0].tablet_id(), test_client()));
  ASSERT_OK(GetLastActiveTimeFromCdcStateTable(stream_id, tablets2[0].tablet_id(), test_client()));

  // Test for the following -
  //  1) cdc_state table entry for (cs_stream_id, tablet1) should be present
  //  2) cdc_state table entry for (cs_stream_id, tablet2) should still be present
  //  3) Retention barriers on tablet1 must be in place
  //  4) Retention barriers on tablet2 should be released
  //  5) GetChanges on stream_id should work for both tablet1 and tablet2
  SleepFor(MonoDelta::FromSeconds(5));
  ASSERT_OK(GetLastActiveTimeFromCdcStateTable(
      cs_stream_id, tablets1[0].tablet_id(), test_client()));
  ASSERT_OK(GetLastActiveTimeFromCdcStateTable(
      cs_stream_id, tablets2[0].tablet_id(), test_client()));
  ASSERT_NE(tablet1_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_LT(tablet1_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  ASSERT_LT(tablet1_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());
  ASSERT_EQ(tablet2_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());
  ASSERT_EQ(tablet2_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  ASSERT_EQ(tablet2_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_OK(SetCDCCheckpoint(stream_id, tablets1));
  ASSERT_OK(GetChangesFromCDCSnapshot(stream_id, tablets1));
  ASSERT_OK(SetCDCCheckpoint(stream_id, tablets2));
  ASSERT_OK(GetChangesFromCDCSnapshot(stream_id, tablets2));

}

TEST_F(CDCSDKConsistentSnapshotTest, TestReleaseResourcesOnUnpolledSplitTablets) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;

  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  // Create the consistent snapshot stream
  ASSERT_RESULT(CreateConsistentSnapshotStream());

  ASSERT_OK(SplitTablet(tablets.Get(0).tablet_id(), &test_cluster_));
  SleepFor(MonoDelta::FromSeconds(60));
  // WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  LOG(INFO) << "Tablet split succeeded";

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_split.size(), num_tablets * 2);

  // Check that parent and child tablets have valid retention barriers
  for (const auto& tablet : {tablets[0], tablets_after_split[0], tablets_after_split[1]}) {
    auto tablet_peer =
        ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablet.tablet_id()));
    LogRetentionBarrierDetails(tablet_peer);
    if (tablet.tablet_id() == tablets[0].tablet_id()) {
      ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
    } else {
      ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
    }
    ASSERT_LT(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
    ASSERT_LT(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());
  }

  // Now, sleep and make stream indicate no interest in all tablets
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_tablet_not_of_interest_timeout_secs) = 3;
  SleepFor(MonoDelta::FromSeconds(8));

  // Check that retention barriers have been released on parent and child tablets
  for (const auto& tablet : {tablets[0], tablets_after_split[0], tablets_after_split[1]}) {
    auto tablet_peer =
        ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablet.tablet_id()));
    LogRetentionBarrierDetails(tablet_peer);
    ASSERT_EQ(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());
    ASSERT_EQ(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
    ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  }

}

TEST_F(CDCSDKConsistentSnapshotTest, TestReleaseResourcesWhenNoStreamsOnTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 120;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));

  // Create a Consistent Snapshot Stream
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
  ASSERT_TRUE(DeleteCDCStream(stream_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 5;
  VerifyTransactionParticipant(tablets[0].tablet_id(), OpId::Max());

}

}  // namespace cdc
}  // namespace yb
