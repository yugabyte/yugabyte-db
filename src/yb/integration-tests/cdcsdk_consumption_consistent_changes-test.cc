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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/integration-tests/cdcsdk_ysql_test_base.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

class CDCSDKConsumptionConsistentChangesTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_consumption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_cdcsdk_setting_get_changes_response_byte_limit) = true;

    // TODO(#30298): Port all the tests to run with mechanism to poll sys catalog tablet. Certain
    // tests written exclusively for pub refresh mechanism should be run with this flag disabled.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
        false;
  }

  enum FeedbackType {
    // LSN of the COMMIT record of the acknowledged txn
    commit_lsn = 1,
    // LSN of the COMMIT record of the acknowledged txn + 1. Walsender follows this feedback
    // mechanism.
    commit_lsn_plus_one = 2,
    // LSN of the BEGIN record of the txn after the acknowledged txn
    begin_lsn = 3
  };
  void TestVWALRestartOnFullTxnAck(FeedbackType feedback_type);
  void TestVWALRestartOnMultiThenSingleShardTxn(FeedbackType feedback_type);
  void TestVWALRestartOnLongTxns(FeedbackType feedback_type);
  void TestConcurrentConsumptionFromMultipleVWAL(CDCSDKSnapshotOption snapshot_option);
  void TestCommitTimeTieWithPublicationRefreshRecord(bool special_record_in_separate_response);
  void TestSlotRowDeletion(bool multiple_streams);
  void TestColocatedUpdateWithIndex(bool use_pk_as_index);
  void TestColocatedUpdateAffectingNoRows(bool use_multi_shard);
  void TestExplcictCheckpointMovementAfterDDL(bool no_activity_post_ddl);
};

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVirtualWAL) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test_table ($0, $1) select i, i+1 from generate_series(1,50) as i",
      kKeyColumnName, kValueColumnName));
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId1));

  // Sending the same session_id should result in error because a corresponding VirtualWAL instance
  // will already exist.
  ASSERT_NOK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId1));

  // Empty table list should succeed.
  ASSERT_OK(InitVirtualWAL(stream_id, {}, kVWALSessionId2));
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId3));

  ASSERT_OK(DestroyVirtualWAL(kVWALSessionId2));
  ASSERT_OK(DestroyVirtualWAL(kVWALSessionId3));

  // Sending the same session_id should result in error because the corresponding VirtualWAL
  // instance would have already been deleted.
  ASSERT_NOK(DestroyVirtualWAL(kVWALSessionId3));

  ASSERT_OK(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));

  // Sending a different session_id for which VirtualWAL does not exist should result in error.
  ASSERT_NOK(GetConsistentChangesFromCDC(stream_id, kVWALSessionId3));
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestExplicitCheckpointForSingleShardTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 50;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int, value_1 int, PRIMARY KEY (ID ASC)) SPLIT AT VALUES "
                   "((1000), (2000))"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  std::unordered_map<TabletId, CdcStateTableRow> initial_tablet_checkpoint;
  for (const auto& tablet : tablets) {
    auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id, tablet.tablet_id()));
    initial_tablet_checkpoint[tablet.tablet_id()] = result;
  }

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  for (int j = 0; j < 600; j++) {
    ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
  }

  int expected_dml_records = 600;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  auto last_record =
      get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
  VerifyLastRecordAndProgressOnSlot(stream_id, last_record);

  std::unordered_set<TabletId> expected_tablet_ids_with_progress;
  // GetTablets calls GetTableLocations RPC which seem to be iterating over an ordered map of
  // partitions. So, we can be sure that tablet at idx 0 is the one where all the inserts have been
  // done.
  expected_tablet_ids_with_progress.insert(tablets[0].tablet_id());
  VerifyExplicitCheckpointingOnTablets(
      stream_id, initial_tablet_checkpoint, tablets, expected_tablet_ids_with_progress);

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestMajorityReplicatedIndexPresentInClosedWalSegment) {
  int num_tservers = 3;
  int tablet_split_range_value = 10;
  ASSERT_OK(SetUpWithParams(num_tservers, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test1 (id int, value_1 int, PRIMARY KEY (ID ASC)) SPLIT AT VALUES "
      "(($0))",
      tablet_split_range_value));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  std::unordered_map<TabletId, master::TabletLocationsPB> tablets_locations_map;
  for (const auto& tablet : tablets) {
    tablets_locations_map[tablet.tablet_id()] = tablet;
  }
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Perform 1 insert each on both the tablets and consume them so that the from_op_id moves past
  // the initial DDL record on both the tablets. This is required so that the response safe time
  // does not get set to the commit_time of this record as it is a relevant record for CDC.
  ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES (0, 0)"));
  ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $0)", tablet_split_range_value));

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId1));
  int expected_dml_records = 2;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */,
      kVWALSessionId1));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";
  // 2 * (BEGIN + DML + COMMIT )
  ASSERT_EQ(get_consistent_changes_resp.records.size(), 6);

  // We require some new WAL OPs on tablet-1 that are non-relevant to CDC. Hence, perform a txn on
  // tablet-1 and abort it. All WAL OPs related to this aborted txn will be read but will be
  // filtered out as they are not relevant for CDC.
  int inserts_per_tablet = 4;
  ASSERT_OK(conn1.Execute("BEGIN"));
  for (int i = 1; i <= inserts_per_tablet; i++) {
    ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", i, i));
  }
  ASSERT_OK(conn1.Execute("ABORT"));

  // Explicitly rollover on tablet-1 where the above aborted txn was performed. This is done so that
  // the majority replicated opid & committed opid remains in segment-1 but active segment number
  // points to segment-2.
  // On tablet-1, WAL segment-1 should look as follows:
  // 1.2 CHANGE METADATA OP
  // 1.3 WRITE OP (single-shard txn)
  // 1.4 WRITE OP (aborted multi-shard txn)
  // 1.5 WRITE OP (aborted multi-shard txn)
  // 1.6 WRITE OP (aborted multi-shard txn)
  // 1.7 WRITE OP (aborted multi-shard txn)
  log::SegmentSequence segments;
  for (int i = 0; i < num_tservers; i++) {
    for (const auto& peer : test_cluster()->GetTabletPeers(i)) {
      auto tablet_id = peer->tablet_id();
      auto raft_consensus = ASSERT_RESULT(peer->GetRaftConsensus());
      if (tablets_locations_map.contains(tablet_id)) {
        auto txn_participant = ASSERT_RESULT(peer->shared_tablet())->transaction_participant();
        ASSERT_OK(WaitFor(
            [&]() -> Result<bool> {
              if (txn_participant->GetNumRunningTransactions() == 0) {
                return true;
              }
              return false;
            },
            MonoDelta::FromSeconds(60), "Timed out waiting for running txns reduce to 0"));
        auto committed_op_id_index = raft_consensus->GetLastCommittedOpId().index;
        if (committed_op_id_index >= 7) {
          LOG(INFO) << "Aborted transactions performed on tablet_id: " << tablet_id;
          ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());
          ASSERT_OK(WaitFor(
              [&]() -> Result<bool> {
                auto active_segment_number = peer->log()->active_segment_sequence_number();
                RETURN_NOT_OK(peer->log()->GetSegmentsSnapshot(&segments));
                if (active_segment_number == 2 && segments.size() == 2) {
                  return true;
                }
                return false;
              },
              MonoDelta::FromSeconds(60),
              "Timed out waiting for active segment number to increment"));
        }
      }
    }
  }

  // Perform inserts on tablet-2.
  for (int j = tablet_split_range_value + 1; j <= (tablet_split_range_value + inserts_per_tablet);
       j++) {
    ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j));
  }

  // We should receive the inserts performed on tablet-2.
  expected_dml_records = inserts_per_tablet;
  get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */,
      kVWALSessionId1));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  auto last_record =
      get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
  VerifyLastRecordAndProgressOnSlot(stream_id, last_record);

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records, 0, 4);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestExplicitCheckpointForMultiShardTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test1 (id int, value_1 int, PRIMARY KEY (ID ASC)) SPLIT AT VALUES ((2500))"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  std::unordered_map<TabletId, CdcStateTableRow> initial_tablet_checkpoint;
  for (const auto& tablet : tablets) {
    auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id, tablet.tablet_id()));
    initial_tablet_checkpoint[tablet.tablet_id()] = result;
  }

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 50;
  int inserts_per_batch = 100;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  int expected_dml_records = num_batches * inserts_per_batch;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  auto last_record =
      get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
  VerifyLastRecordAndProgressOnSlot(stream_id, last_record);

  std::unordered_set<TabletId> expected_tablet_ids_with_progress;
  for (const auto& tablet : tablets) {
    expected_tablet_ids_with_progress.insert(tablet.tablet_id());
  }

  VerifyExplicitCheckpointingOnTablets(
      stream_id, initial_tablet_checkpoint, tablets, expected_tablet_ids_with_progress);
  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

void CDCSDKConsumptionConsistentChangesTest::TestConcurrentConsumptionFromMultipleVWAL(
    CDCSDKSnapshotOption snapshot_option) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int, value_1 int, PRIMARY KEY (ID ASC)) SPLIT AT VALUES "
                   "((800), (1600))"));
  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int, value_1 int, PRIMARY KEY (ID ASC)) SPLIT AT VALUES "
                   "((1500), (3000))"));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));

  // insert 50 records in both tables. This will not received in streaming phase.
  for (int i = 0; i < 50; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", i, i + 1));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", i, i + 1));
  }
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table_1_tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table_2_tablets;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &table_1_tablets, nullptr));
  ASSERT_EQ(table_1_tablets.size(), 3);
  ASSERT_OK(test_client()->GetTablets(table2, 0, &table_2_tablets, nullptr));
  ASSERT_EQ(table_2_tablets.size(), 3);
  std::unordered_set<TabletId> expected_table_one_tablets_with_progress;
  std::unordered_set<TabletId> expected_table_two_tablets_with_progress;

  auto stream_id_1 =
      ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot(snapshot_option));

  std::unordered_map<TabletId, CdcStateTableRow> initial_tablet_checkpoint_table_1;
  for (const auto& tablet : table_1_tablets) {
    auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_1, tablet.tablet_id()));
    initial_tablet_checkpoint_table_1[tablet.tablet_id()] = result;
    expected_table_one_tablets_with_progress.insert(tablet.tablet_id());
  }

  auto stream_id_2 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  std::unordered_map<TabletId, CdcStateTableRow> initial_tablet_checkpoint_table_2;
  for (const auto& tablet : table_2_tablets) {
    auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_2, tablet.tablet_id()));
    initial_tablet_checkpoint_table_2[tablet.tablet_id()] = result;
    expected_table_two_tablets_with_progress.insert(tablet.tablet_id());
  }

  int num_batches = 50;
  int inserts_per_batch = 50;
  for (int i = 1; i <= num_batches; i++) {
    ASSERT_OK(conn.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  // Perform 50 more txns only on table 2.
  for (int i = 51; i <= (num_batches + 50); i++) {
    ASSERT_OK(conn.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  int expected_dml_records_table_1 =
      num_batches * inserts_per_batch;  // starting from [50, 100) to [2500, 2550)
  int expected_dml_records_table_2 =
      2 * (num_batches * inserts_per_batch);  // starting from [50, 100) to [5000, 5050)
  std::thread t1([&]() -> void {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
        stream_id_1, {table1.table_id()}, expected_dml_records_table_1, true /* init_virtual_wal */,
        kVWALSessionId1));
    LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 1.";

    auto last_record =
        get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
    VerifyLastRecordAndProgressOnSlot(stream_id_1, last_record);
    VerifyExplicitCheckpointingOnTablets(
        stream_id_1, initial_tablet_checkpoint_table_1, table_1_tablets,
        expected_table_one_tablets_with_progress);
    CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
    CheckRecordCount(get_consistent_changes_resp, expected_dml_records_table_1);
  });

  std::thread t2([&]() -> void {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
        stream_id_2, {table2.table_id()}, expected_dml_records_table_2, true /* init_virtual_wal */,
        kVWALSessionId2));
    LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 2.";

    auto last_record =
        get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
    VerifyLastRecordAndProgressOnSlot(stream_id_2, last_record);
    VerifyExplicitCheckpointingOnTablets(
        stream_id_2, initial_tablet_checkpoint_table_2, table_2_tablets,
        expected_table_two_tablets_with_progress);
    CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
    CheckRecordCount(get_consistent_changes_resp, expected_dml_records_table_2);
  });

  t1.join();
  t2.join();
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestParallelConsumptionFromMultipleVWALWithExportSnapshot) {
  TestConcurrentConsumptionFromMultipleVWAL(CDCSDKSnapshotOption::EXPORT_SNAPSHOT);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestParallelConsumptionFromMultipleVWALWithNoSnapshot) {
  TestConcurrentConsumptionFromMultipleVWAL(CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT);
}

void CDCSDKConsumptionConsistentChangesTest::TestVWALRestartOnFullTxnAck(
    FeedbackType feedback_type) {
  // This test basically aims to verify the client does not receive a txn again after a restart if
  // it has acknowledged it completely. Test performs the following steps:
  // 1. Perform 5 multi-shard txns.
  // 2. Consume all 5 txns
  // 3. Acknowledge 3rd txn
  // 4. Destroy Virtual WAL ~ Restart
  // 5. Consume records & verify we receive 4 & 5th txns.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 5;
  int inserts_per_batch = 30;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  int expected_dml_records = num_batches * inserts_per_batch;
  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId1, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp.records.size() << " records.";

  // confirmed_flush would always point to COMMIT record of 3rd txn.
  uint64_t confirmed_flush_lsn = 0;
  // restart_lsn will be set based on the feedback type.
  uint64_t restart_lsn = 0;
  // This will hold records of 4th & 5th txn.
  vector<CDCSDKProtoRecordPB> expected_records;
  for (const auto& record : resp.records) {
    if (record.row_message().pg_transaction_id() == 4 &&
        record.row_message().op() == RowMessage_Op_COMMIT) {
      confirmed_flush_lsn = record.row_message().pg_lsn();

      if (feedback_type != FeedbackType::begin_lsn) {
        restart_lsn = confirmed_flush_lsn;
        if (feedback_type == FeedbackType::commit_lsn_plus_one) {
          LOG(INFO) << "Incrementing commit_lsn " << restart_lsn << " by 1";
          restart_lsn += 1;
        }
      }
    }

    if (feedback_type == FeedbackType::begin_lsn && record.row_message().pg_transaction_id() == 5 &&
        record.row_message().op() == RowMessage_Op_BEGIN) {
      restart_lsn = record.row_message().pg_lsn();
    }

    if (record.row_message().pg_transaction_id() >= 5) {
      expected_records.push_back(record);
    }
  }

  ASSERT_GT(confirmed_flush_lsn, 0);
  ASSERT_GT(restart_lsn, 0);
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, kVWALSessionId1));

  // Call GetConsistentChanges twice so as to send explicit checkpoint on both the tablets. Two
  // calls because in one call, only 1 tablet queue would be empty and the other tablet queue would
  // have a safepoint record. So, we'll call GetChanges only the empty tablet queue.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
  // Since we had already consumed all records, we dont expect any records in these
  // GetConsistentChanges calls.
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(DestroyVirtualWAL(kVWALSessionId1));

  expected_dml_records = inserts_per_batch * 2;  // 4 & 5th txn expected
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId2, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";
  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId2));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_EQ(resp_after_restart.records.size(), expected_records.size());
  for (int i = 0; i < static_cast<int>(expected_records.size()); i++) {
    auto record_row_message = resp_after_restart.records[i].row_message();
    auto expected_record_row_message = expected_records[i].row_message();
    ASSERT_EQ(record_row_message.op(), expected_record_row_message.op());
    ASSERT_EQ(record_row_message.pg_lsn(), expected_record_row_message.pg_lsn());
    ASSERT_EQ(
        record_row_message.pg_transaction_id(), expected_record_row_message.pg_transaction_id());
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnFullTxnAckCommitLsn) {
  TestVWALRestartOnFullTxnAck(FeedbackType::commit_lsn);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnFullTxnAckCommitLsnPlusOne) {
  TestVWALRestartOnFullTxnAck(FeedbackType::commit_lsn_plus_one);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnFullTxnAckBeginLsn) {
  TestVWALRestartOnFullTxnAck(FeedbackType::begin_lsn);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnPartialTxnAck) {
  // This test basically aims to verify the client get the entire txn again after a restart if the
  // client has acknowledged it partially i.e. sent feedback corresponding to a DML record of that
  // txn. Test performs the following steps:
  // 1. Perform 5 multi-shard txns.
  // 2. Consume all 5 txns
  // 3. Acknowledge a DML in 3rd txn
  // 4. Destroy Virtual WAL ~ Restart
  // 5. Consume records & verify we receive 3rd, 4th & 5th txns.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 5;
  int inserts_per_batch = 30;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  int expected_dml_records = num_batches * inserts_per_batch;
  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId1, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp.records.size() << " records.";

  // confirmed_flush would always point to LSN of the DML record of 3rd txn.
  uint64_t confirmed_flush_lsn = 0;
  uint64_t restart_lsn = 0;
  // This will hold records of 3rd, 4th & 5th txn.
  vector<CDCSDKProtoRecordPB> expected_records;
  for (const auto& record : resp.records) {
    if (record.row_message().pg_transaction_id() == 4 && IsDMLRecord(record)) {
      confirmed_flush_lsn = record.row_message().pg_lsn();
      restart_lsn = confirmed_flush_lsn;
    }

    if (record.row_message().pg_transaction_id() >= 4) {
      expected_records.push_back(record);
    }
  }

  ASSERT_GT(confirmed_flush_lsn, 0);
  ASSERT_GT(restart_lsn, 0);
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, kVWALSessionId1));

  // Call GetConsistentChanges twice so as to send explicit checkpoint on both the tablets. Two
  // calls because in one call, only 1 tablet queue would be empty and the other tablet queue would
  // have a safepoint record. So, we'll call GetChanges only the empty tablet queue.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
  // Since we had already consumed all records, we dont expect any records in these
  // GetConsistentChanges calls.
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(DestroyVirtualWAL(kVWALSessionId1));

  expected_dml_records = inserts_per_batch * 3;  // last 3 txns expected
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId2, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";
  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId2));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_EQ(resp_after_restart.records.size(), expected_records.size());
  for (int i = 0; i < static_cast<int>(expected_records.size()); i++) {
    auto record_row_message = resp_after_restart.records[i].row_message();
    auto expected_record_row_message = expected_records[i].row_message();
    ASSERT_EQ(record_row_message.op(), expected_record_row_message.op());
    ASSERT_EQ(record_row_message.pg_lsn(), expected_record_row_message.pg_lsn());
    ASSERT_EQ(
        record_row_message.pg_transaction_id(), expected_record_row_message.pg_transaction_id());
  }
}

void CDCSDKConsumptionConsistentChangesTest::TestVWALRestartOnMultiThenSingleShardTxn(
    FeedbackType feedback_type) {
  // This test basically aims to verify that there is no data loss even after a restart
  // despite sending from_checkpoint as the explicit checkpoint. Test performs the following steps:
  // 1. Perform 5 multi-shard txns.
  // 2. Perform a single-shard txn (6th txn).
  // 3. Consume all 5 txns
  // 4. Acknowledge 5th txn
  // 5. Call GetConsistentChanges that will in turn call GetChanges on empty tablet queue with
  // explicit checkpoint as the from_checkpoint.
  // 6. Destroy Virtual WAL ~ Restart
  // 7. Consume records & verify we receive 6th txn.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 5_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 160;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 5;
  int inserts_per_batch = 30;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES (150, 151)"));

  int expected_dml_records = num_batches * inserts_per_batch;
  // We would not receive the single-shard txn in the response because we have set the max records
  // in GetConsistentChanges response (FLAGS_cdcsdk_max_consistent_records) to 160 i.e 30 DML +
  // BEGIN-COMMIT for each of the 5 txn.
  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId1, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp.records.size() << " records.";

  // confirmed_flush would always point to LSN of the COMMIT record of 5th txn.
  uint64_t confirmed_flush_lsn = 0;
  uint64_t restart_lsn = 0;
  uint32_t last_received_txn_id = 0;
  for (const auto& record : resp.records) {
    last_received_txn_id = record.row_message().pg_transaction_id();
    if (record.row_message().pg_transaction_id() == 6) {
      confirmed_flush_lsn = record.row_message().pg_lsn();
      restart_lsn = confirmed_flush_lsn;
    }
  }

  ASSERT_EQ(last_received_txn_id, 6);
  ASSERT_EQ(resp.records.size(), 160);
  ASSERT_GT(confirmed_flush_lsn, 0);
  ASSERT_GT(restart_lsn, 0);

  if (feedback_type == FeedbackType::commit_lsn_plus_one) {
    LOG(INFO) << "Incrementing commit_lsn " << restart_lsn << " by 1";
    restart_lsn += 1;
  }
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, kVWALSessionId1));

  // Out of the 2 tablet queues, one of them would contain the single-shard txn (150,151) and the
  // other one would be empty. So, in the next GetConsistentChanges, we will use from_checkpoint as
  // the explicit checkpoint while making GetChanges calls on the empty tablet queue.
  expected_dml_records = 1;
  resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */,
      kVWALSessionId1, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp.records.size() << " records.";
  ASSERT_EQ(resp.records.size(), 3);

  // This will hold records of 6th txn.
  vector<CDCSDKProtoRecordPB> expected_records = resp.records;

  ASSERT_OK(DestroyVirtualWAL(kVWALSessionId1));

  expected_dml_records = 1;
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId2, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";

  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId2));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_EQ(resp_after_restart.records.size(), expected_records.size());
  for (int i = 0; i < static_cast<int>(resp_after_restart.records.size()); i++) {
    auto record_row_message = resp_after_restart.records[i].row_message();
    auto expected_record_row_message = expected_records[i].row_message();
    ASSERT_EQ(record_row_message.op(), expected_record_row_message.op());
    ASSERT_EQ(record_row_message.pg_lsn(), expected_record_row_message.pg_lsn());
    ASSERT_EQ(
        record_row_message.pg_transaction_id(), expected_record_row_message.pg_transaction_id());
  }
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnMultiThenSingleShardTxnAckCommitLsn) {
  TestVWALRestartOnMultiThenSingleShardTxn(FeedbackType::commit_lsn);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestVWALRestartOnMultiThenSingleShardTxnAckCommitLsnPlusOne) {
  TestVWALRestartOnMultiThenSingleShardTxn(FeedbackType::commit_lsn_plus_one);
}

void CDCSDKConsumptionConsistentChangesTest::TestVWALRestartOnLongTxns(FeedbackType feedback_type) {
  // This test basically aims to verify that the client is able to get records even after a restart
  // when it acknowledged a commit record that also happens to be the last shipped commit from VWAL.
  // Test performs the following steps:
  // 1. Perform 4 long txns.
  // 2. Configure GetChanges such that it sends partial records of the long txn. Consume it via
  // GetConsistentChanges.
  // 3. Acknowledge the 3rd txn.
  // 4. Call GetConsistentChanges that will in turn call GetChanges on empty tablet queue with
  // explicit checkpoint.
  // 5. Destroy Virtual WAL ~ Restart
  // 6. Consume records & verify we receive only the 4th txn.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 4;
  int inserts_per_batch = 1000;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  // This will hold records of 4th txn.
  vector<CDCSDKProtoRecordPB> expected_records;

  // confirmed_flush_lsn would point to the COMMIT record of the 3rd txn.
  uint64_t confirmed_flush_lsn = 0;
  // Acknowledge 3rd txn.
  uint64_t restart_lsn;

  while (expected_records.size() == 0) {
    auto resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    for (const auto& record : resp.cdc_sdk_proto_records()) {
      if (record.row_message().pg_transaction_id() == 4 &&
          record.row_message().op() == RowMessage_Op_COMMIT) {
        confirmed_flush_lsn = record.row_message().pg_lsn();

        if (feedback_type != FeedbackType::begin_lsn) {
          restart_lsn = confirmed_flush_lsn;
          if (feedback_type == FeedbackType::commit_lsn_plus_one) {
            LOG(INFO) << "Incrementing commit_lsn " << restart_lsn << " by 1";
            restart_lsn += 1;
          }
        }
      }

      if (feedback_type == FeedbackType::begin_lsn &&
          record.row_message().pg_transaction_id() == 5 &&
          record.row_message().op() == RowMessage_Op_BEGIN) {
        restart_lsn = record.row_message().pg_lsn();
      }

      if (record.row_message().pg_transaction_id() == 5) {
        expected_records.push_back(record);
      }
    }
  }

  ASSERT_GT(confirmed_flush_lsn, 0);
  ASSERT_GT(restart_lsn, 0);
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, kVWALSessionId1));

  // Consume the 4th txn completely i.e 1 BEGIN + 1000 DML + 1 COMMIT.
  while (expected_records.size() != 1002) {
    auto get_consistent_changes_resp =
        ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
    LOG(INFO) << "Got " << get_consistent_changes_resp.cdc_sdk_proto_records_size() << " records.";
    ASSERT_GT(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);
    for (const auto& record : get_consistent_changes_resp.cdc_sdk_proto_records()) {
      expected_records.push_back(record);
    }
  }

  // To make sure that we have sent an explicit checkpoint against which we had earlier sent a
  // feedback ,we make this GetConsistentChanges call. After fully consuming 4th txn, the tablet
  // queues would be empty, hence on in this GetConsistent call, Virtual WAL will be making
  // GetChanges call on the empty tablet queue, thereby also sending explicit checkpoint. We would
  // receive 0 records in the resposne since we have already consumed everything.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
  LOG(INFO) << "Got " << get_consistent_changes_resp.cdc_sdk_proto_records_size() << " records.";
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(DestroyVirtualWAL(kVWALSessionId1));

  int expected_dml_records = inserts_per_batch * 1;
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId2, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";

  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId2));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_EQ(resp_after_restart.records.size(), expected_records.size());
  for (int i = 0; i < static_cast<int>(expected_records.size()); i++) {
    auto record_row_message = resp_after_restart.records[i].row_message();
    auto expected_record_row_message = expected_records[i].row_message();
    ASSERT_EQ(record_row_message.op(), expected_record_row_message.op());
    ASSERT_EQ(record_row_message.pg_lsn(), expected_record_row_message.pg_lsn());
    ASSERT_EQ(
        record_row_message.pg_transaction_id(), expected_record_row_message.pg_transaction_id());
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnLongTxnsAckCommitLsn) {
  TestVWALRestartOnLongTxns(FeedbackType::commit_lsn);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnLongTxnsAckCommitLsnPlusOne) {
  TestVWALRestartOnLongTxns(FeedbackType::commit_lsn_plus_one);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALRestartOnLongTxnsAckBeginLsn) {
  TestVWALRestartOnLongTxns(FeedbackType::begin_lsn);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithGenerateSeries) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 250;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test_table ($0, $1) select i, i+1 from generate_series(1,1000) as i",
      kKeyColumnName, kValueColumnName));

  int expected_dml_records = 1000;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithManyTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 50 / kTimeMultiplier;
  int inserts_per_batch = 100 / kTimeMultiplier;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  int expected_dml_records = 2 * num_batches * inserts_per_batch;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithForeignKeys) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 50;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1(id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2(id int primary key, value_2 int, test1_id int, CONSTRAINT "
                   "fkey FOREIGN KEY(test1_id) REFERENCES test1(id)) SPLIT INTO 3 TABLETS"));

  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table2, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  ASSERT_OK(conn.Execute("INSERT INTO test1 VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 VALUES (2, 2)"));

  int queries_per_batch = 50;
  int num_batches = 30;
  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, queries_per_batch, "INSERT INTO test2 VALUES ($0, 1, 1)", 20);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, queries_per_batch, "INSERT INTO test2 VALUES ($0, 1, 1)", 50,
        num_batches * queries_per_batch);
  });

  t1.join();
  t2.join();

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, queries_per_batch, "UPDATE test2 SET test1_id=2 WHERE id = $0", 30);
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, queries_per_batch, "UPDATE test2 SET test1_id=2 WHERE id = $0", 50,
        num_batches * queries_per_batch);
  });

  t3.join();
  t4.join();

  int expected_dml_records = 2 * (2 * num_batches * queries_per_batch);
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table2.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithoutPrimaryKey) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_cdcsdk_stream_tables_without_primary_key) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key int, value_1 int) SPLIT INTO 1 TABLETS", kTableName));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));

  vector<string> replica_identities = {"CHANGE", "DEFAULT", "FULL", "NOTHING"};

  // Expected tuples for replica identites "CHANGE", "DEFAULT", "FULL", "NOTHING" respectively.
  vector<vector<string>> expected_new_tuples_for_insert = {
      {"ybrowid", "key", "value_1"},
      {"ybrowid", "key", "value_1"},
      {"ybrowid", "key", "value_1"},
      {"ybrowid", "key", "value_1"}};
  vector<vector<string>> expected_old_tuples_for_insert = {{}, {}, {}, {}};

  vector<vector<string>> expected_new_tuples_for_update = {
      {"ybrowid", "value_1"},
      {"ybrowid", "value_1", "key"},
      {"ybrowid", "value_1", "key"},
      {"ybrowid", "value_1", "key"}};
  vector<vector<string>> expected_old_tuples_for_update = {
      {}, {}, {"ybrowid", "key", "value_1"}, {}};

  vector<vector<string>> expected_new_tuples_for_delete = {{}, {}, {}, {}};
  vector<vector<string>> expected_old_tuples_for_delete = {
      {"ybrowid"}, {"ybrowid"}, {"ybrowid", "key", "value_1"}, {"ybrowid"}};

  for (int i = 0; i < static_cast<int>(replica_identities.size()); i++) {
    ASSERT_OK(conn.ExecuteFormat(
        "ALTER TABLE $0 REPLICA IDENTITY $1", kTableName, replica_identities[i]));

    auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

    ASSERT_OK(WriteRows(1, 2, &test_cluster_));
    ASSERT_OK(UpdateRows(1, 100, &test_cluster_));
    ASSERT_OK(DeleteRows(1, &test_cluster_));

    ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

    auto resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_EQ(resp.cdc_sdk_proto_records_size(), 9);

    for (int i = 0; i < 3; i++) {
      // Records with indices 0, 3, 6 are BEGIN records.
      ASSERT_EQ(resp.cdc_sdk_proto_records(i * 3).row_message().op(), RowMessage_Op_BEGIN);
      // Records with indices 2, 5, 8 are COMMIT records.
      ASSERT_EQ(resp.cdc_sdk_proto_records(i * 3 + 2).row_message().op(), RowMessage_Op_COMMIT);
    }

    // Record with index 1 is an INSERT record.
    ASSERT_EQ(resp.cdc_sdk_proto_records(1).row_message().op(), RowMessage_Op_INSERT);
    CheckRecordTuples(
        resp.cdc_sdk_proto_records(1), expected_new_tuples_for_insert[i],
        expected_old_tuples_for_insert[i]);

    // Record with index 4 is an UPDATE record.
    ASSERT_EQ(resp.cdc_sdk_proto_records(4).row_message().op(), RowMessage_Op_UPDATE);
    CheckRecordTuples(
        resp.cdc_sdk_proto_records(4), expected_new_tuples_for_update[i],
        expected_old_tuples_for_update[i]);

    // Record with index 7 is a DELETE record.
    ASSERT_EQ(resp.cdc_sdk_proto_records(7).row_message().op(), RowMessage_Op_DELETE);
    CheckRecordTuples(
        resp.cdc_sdk_proto_records(7), expected_new_tuples_for_delete[i],
        expected_old_tuples_for_delete[i]);

    ASSERT_OK(DestroyVirtualWAL());
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithAbortedTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_consistent_records) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 10;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // COMMIT
  ASSERT_OK(WriteRowsHelper(1, 10, &test_cluster_, true));

  // ABORT
  ASSERT_OK(WriteRowsHelper(10, 20, &test_cluster_, false));

  // ABORT
  ASSERT_OK(WriteRowsHelper(20, 30, &test_cluster_, false));

  // COMMIT
  ASSERT_OK(WriteRowsHelper(30, 40, &test_cluster_, true));

  // ROLLBACK
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 0; i < 10; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_table VALUES ($0, 1)", i + 40));
  }
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // END
  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 0; i < 10; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_table VALUES ($0, 1)", i + 50));
  }
  ASSERT_OK(conn.Execute("END"));

  int expected_dml_records = 29;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithColocation) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP tg1"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test1(id int primary key, value_1 int) TABLEGROUP tg1;"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test2(id int primary key, value_1 int) TABLEGROUP tg1;"));

  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 50;
  int inserts_per_batch = 50;

  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test1 VALUES ($0, 1)", 20);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test1 VALUES ($0, 1)", 50,
        num_batches * inserts_per_batch);
  });
  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test2 VALUES ($0, 1)", 20);
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test2 VALUES ($0, 1)", 50,
        num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();
  t3.join();
  t4.join();

  int expected_dml_records = 4 * num_batches * inserts_per_batch;
  vector<TableId> table_ids = {table1.table_id(), table2.table_id()};
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, table_ids, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALConsumptionOnMixTables) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 15;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 40;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  // Create 2 colocated + 1 non-colocated table
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP tg1"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test1(id int primary key, value_1 int) TABLEGROUP tg1;"));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE test2(id int primary key, value_1 int) TABLEGROUP tg1;"));

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test3(id int primary key, value_1 int) SPLIT INTO 3 TABLETS;"));

  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  auto table3 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test3"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_table_3;
  ASSERT_OK(test_client()->GetTablets(table3, 0, &tablets_table_3, nullptr));
  ASSERT_EQ(tablets_table_3.size(), 3);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 2;
  int inserts_per_batch = 20;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", j, j + 1));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test3 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  ASSERT_OK(conn.Execute("BEGIN"));
  for (int j = 40; j < 60; j++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test3 VALUES ($0, $1)", j, j + 1));
  }
  ASSERT_OK(conn.Execute("COMMIT"));

  // Perform some single-shard txns on test1 & test3.
  for (int j = 60; j < 80; j++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test3 VALUES ($0, $1)", j, j + 1));
  }

  ASSERT_OK(conn.Execute("BEGIN"));
  for (int j = 80; j < 100; j++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", j, j + 1));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test3 VALUES ($0, $1)", j, j + 1));
  }
  ASSERT_OK(conn.Execute("COMMIT"));

  // Perform some single-shard txns on test2 & test3.
  for (int j = 100; j < 120; j++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", j, j + 1));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test3 VALUES ($0, $1)", j, j + 1));
  }

  // Expected DML = num_batches * inserts_per_batch/table * 3 table + 40 insert/table * 2 tables
  // (test1,test3) + 20 inserts/table * 2 tables (test2, test3).
  int expected_dml_records = num_batches * inserts_per_batch * 3 + 40 * 2 + 40 * 2;
  vector<TableId> table_ids = {table1.table_id(), table2.table_id(), table3.table_id()};
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, table_ids, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKMakesProgressWithLongRunningTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_resolve_intent_lag_threshold_ms) =
      10 * 1000 * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);

  // Flushed transactions are replayed only if there is a cdc stream.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Initiate a transaction with 'BEGIN' statement. But do not commit it.
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 0; i < 100; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_table VALUES ($0, $1)", i, i + 1));
  }

  // Commit another transaction while we still have the previous one open.
  ASSERT_OK(WriteRowsHelper(100, 600, &test_cluster_, true));

  uint32 seen_insert_records = 0;
  auto update_insert_count = [&](const GetConsistentChangesResponsePB& change_resp) {
    for (const auto& record : change_resp.cdc_sdk_proto_records()) {
      if (record.row_message().op() == RowMessage::INSERT) {
        seen_insert_records += 1;
      }
    }
  };

  // Initially we will not see any records, even though we have a committed transaction, because the
  // running transaction holds back the consistent_safe_time.
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  GetConsistentChangesResponsePB change_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  update_insert_count(change_resp);
  ASSERT_EQ(seen_insert_records, 0);
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  update_insert_count(change_resp);
  ASSERT_EQ(seen_insert_records, 0);

  vector<CDCSDKProtoRecordPB> records;
  // Eventually, after FLAGS_cdc_resolve_intent_lag_threshold_ms time we should see the records for
  // the committed transaction.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        update_insert_count(change_resp);
        for (const auto& record : change_resp.cdc_sdk_proto_records()) {
          records.push_back(record);
        }
        if (seen_insert_records == 500) return true;

        return false;
      },
      MonoDelta::FromSeconds(60), "Did not see all expected records"));

  CheckRecordsConsistencyFromVWAL(records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestConsistentSnapshotWithCDCSDKConsistentStream) {
  google::SetVLOGLevel("cdc*", 0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // GetCheckpoint after snapshot bootstrap (done as part of stream creation itself).
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  int num_batches = 5;
  int inserts_per_batch = 100;

  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20, 201);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, 201 + num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

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
      change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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

    // End of the snapshot records.
    if (change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      change_resp_updated = ASSERT_RESULT(UpdateSnapshotDone(stream_id, tablets));
      break;
    }
  }
  ASSERT_EQ(reads_snapshot, 200);

  int expected_dml_records = 2 * num_batches * inserts_per_batch;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALConsumptionWitDDLStatements) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 2;
  int inserts_per_batch = 8;

  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20 /* apply_update_latency */);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50 /* apply_update_latency */,
        num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD value_2 int;"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP value_1;"));
  // Additional sleep before we make new inserts.
  SleepFor(MonoDelta::FromSeconds(5));

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test_table VALUES ($0, 1)",
        20 /* apply_update_latency */, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test_table VALUES ($0, 1)",
        50 /* apply_update_latency */, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  int expected_dml_records = 4 * num_batches * inserts_per_batch;
  int expected_ddl_records = 2 * 3;  // 2 DDL / tablet
  auto get_all_pending_changes_resp = ASSERT_RESULT(
      GetAllPendingTxnsFromVirtualWAL(stream_id, {table.table_id()}, expected_dml_records, true));

  CheckRecordsConsistencyFromVWAL(get_all_pending_changes_resp.records);
  LOG(INFO) << "Got " << get_all_pending_changes_resp.records.size() << " records.";
  CheckRecordCount(get_all_pending_changes_resp, expected_dml_records, expected_ddl_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALConsumptionWitDDLStatementsAndRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 5;
  int inserts_per_batch = 50;

  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20 /* apply_update_latency */);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50 /* apply_update_latency */,
        num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD value_2 int;"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP value_1;"));

  int expected_dml_records = 2 * num_batches * inserts_per_batch;

  // This method would have consumed all the txns, along with some DDLs. But the last sent feedback
  // would not have sent an acknowledgement to the cdc_service via the GetChanges call.
  auto get_all_pending_changes_resp = ASSERT_RESULT(
      GetAllPendingTxnsFromVirtualWAL(stream_id, {table.table_id()}, expected_dml_records, true));

  size_t idx = get_all_pending_changes_resp.records.size() - 1;
  while (get_all_pending_changes_resp.records[idx].row_message().op() != RowMessage_Op_COMMIT) {
    idx--;
  }
  int last_received_txn_id =
      get_all_pending_changes_resp.records[idx].row_message().pg_transaction_id();

  CheckRecordsConsistencyFromVWAL(get_all_pending_changes_resp.records);
  LOG(INFO) << "Got " << get_all_pending_changes_resp.records.size() << " records.";
  CheckRecordCount(get_all_pending_changes_resp, expected_dml_records);

  ASSERT_OK(DestroyVirtualWAL());

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test_table VALUES ($0, 1)",
        20 /* apply_update_latency */, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test_table VALUES ($0, 1)",
        50 /* apply_update_latency */, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  // We should only receive inserts performed after VWAL restart.
  expected_dml_records = 2 * num_batches * inserts_per_batch;
  int expected_min_txn_id_after_restart = last_received_txn_id + 1;
  // We should receive all 6 DDLs (2 DDLs per tablet) after restart since we havent acknowledged the
  // DDLs to the cdc_service.
  int expected_ddl_records = 2 * 3;
  auto resp_after_restart = ASSERT_RESULT(
      GetAllPendingTxnsFromVirtualWAL(stream_id, {table.table_id()}, expected_dml_records, true));

  // First 6 records recevied after restart should be DDL records.
  for (int i = 0; i < expected_ddl_records; i++) {
    ASSERT_TRUE(resp_after_restart.records[i].row_message().op() == RowMessage_Op_DDL);
  }

  CheckRecordsConsistencyFromVWAL(resp_after_restart.records);
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";
  CheckRecordCount(
      resp_after_restart, expected_dml_records, expected_ddl_records,
      expected_min_txn_id_after_restart);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALConsumptionWithMultipleAlter) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  const int num_tservers = 3;
  ASSERT_OK(SetUpWithParams(num_tservers, 1, false, true));
  const uint32_t num_tablets = 3;
  // Creates a table with a key, and value column.
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Create CDC stream.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  ASSERT_OK(WriteRowsHelper(1 /* start */, 11 /* end */, &test_cluster_, true));
  int expected_dml_records = 10;
  auto all_pending_consistent_change_resp = ASSERT_RESULT(
      GetAllPendingTxnsFromVirtualWAL(stream_id, {table.table_id()}, expected_dml_records, true));
  // Validate the columns and insert counts.
  ValidateColumnCounts(all_pending_consistent_change_resp, 2);
  CheckRecordCount(all_pending_consistent_change_resp, expected_dml_records);

  for (int nonkey_column_count = 2; nonkey_column_count < 15; ++nonkey_column_count) {
    std::string added_column_name = "value_" + std::to_string(nonkey_column_count);
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, added_column_name, &conn));
    ASSERT_OK(WriteRowsHelper(
        nonkey_column_count * 10 + 1 /* start */, nonkey_column_count * 10 + 11 /* end */,
        &test_cluster_, true, 3, kTableName, {added_column_name}));
  }
  LOG(INFO) << "Added columns and pushed required records";

  int expected_ddl_records = 13 * 3;  // 13 DDL / tablet
  expected_dml_records = 13 * 10;     /* number of add columns 'times' insert per batch */
  auto consistent_change_resp_after_alter = ASSERT_RESULT(
      GetAllPendingTxnsFromVirtualWAL(stream_id, {table.table_id()}, expected_dml_records, false));
  LOG(INFO) << "Got " << consistent_change_resp_after_alter.records.size() << " records.";

  int seen_ddl_records = 0;
  int seen_dml_records = 0;
  for (const auto& record : consistent_change_resp_after_alter.records) {
    if (record.row_message().op() == RowMessage::DDL) {
      seen_ddl_records += 1;
    } else if (record.row_message().op() == RowMessage::INSERT) {
      seen_dml_records += 1;
      auto key_value = record.row_message().new_tuple(0).pg_ql_value().int32_value();
      auto expected_column_count = std::ceil(key_value / 10.0);
      ASSERT_EQ(record.row_message().new_tuple_size(), expected_column_count);
    }
  }

  ASSERT_EQ(seen_ddl_records, expected_ddl_records);
  ASSERT_EQ(seen_dml_records, expected_dml_records);
  CheckRecordsConsistencyFromVWAL(consistent_change_resp_after_alter.records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithTabletSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 5;
  int inserts_per_batch = 50;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split two tablets.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 4);
  WaitUntilSplitIsSuccesful(tablets.Get(1).tablet_id(), table, 5);

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));
  ASSERT_EQ(tablets_after_split.size(), 5);

  int expected_dml_records = 4 * num_batches * inserts_per_batch;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestTabletSplitDuringConsumptionFromVWAL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 25;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 5;
  int inserts_per_batch = 40;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  int expected_dml_records = 70;
  int received_dml_records = 0;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  for (const auto& record : get_consistent_changes_resp.records) {
    if (IsDMLRecord(record)) {
      ++received_dml_records;
    }
  }

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split two tablets.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 4);
  WaitUntilSplitIsSuccesful(tablets.Get(1).tablet_id(), table, 5);

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));
  ASSERT_EQ(tablets_after_split.size(), 5);

  int total_dml_performed = 4 * num_batches * inserts_per_batch;
  int leftover_dml_records = total_dml_performed - received_dml_records;
  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, leftover_dml_records, false /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  GetAllPendingChangesResponse final_resp;
  vector<CDCSDKProtoRecordPB> final_records = get_consistent_changes_resp.records;
  for (const auto& record : resp.records) {
    final_records.push_back(record);
  }
  final_resp.records = final_records;
  for (int i = 0; i < 8; ++i) {
    final_resp.record_count[i] = get_consistent_changes_resp.record_count[i] + resp.record_count[i];
  }

  CheckRecordsConsistencyFromVWAL(final_resp.records);
  CheckRecordCount(final_resp, total_dml_performed);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestRecordCountsAfterMultipleTabletSplits) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 25;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 5;
  int inserts_per_batch = 20;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split two tablets.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 4);
  WaitUntilSplitIsSuccesful(tablets.Get(1).tablet_id(), table, 5);

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));
  ASSERT_EQ(tablets_after_split.size(), 5);

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split the two children tablets.
  WaitUntilSplitIsSuccesful(tablets_after_split.Get(0).tablet_id(), table, 6);
  WaitUntilSplitIsSuccesful(tablets_after_split.Get(1).tablet_id(), table, 7);

  std::thread t5([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (4 * num_batches * inserts_per_batch));
  });
  std::thread t6([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (5 * num_batches * inserts_per_batch));
  });

  t5.join();
  t6.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_second_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_second_split, nullptr));
  ASSERT_EQ(tablets_after_second_split.size(), 7);

  int expected_dml_records = 6 * num_batches * inserts_per_batch;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestTabletSplitDuringConsumptionFromVWALWithRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 25;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 5;
  int inserts_per_batch = 40;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  int expected_dml_records = 70;
  int received_dml_records = 0;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  auto last_record_lsn = get_consistent_changes_resp.records.back().row_message().pg_lsn();
  LOG(INFO) << "LSN of last record received: " << last_record_lsn;
  auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id, kCDCSDKSlotEntryTabletId));
  ASSERT_EQ(result.restart_lsn, last_record_lsn);
  ASSERT_EQ(result.confirmed_flush_lsn, last_record_lsn);
  for (const auto& record : get_consistent_changes_resp.records) {
    if (IsDMLRecord(record)) {
      ++received_dml_records;
    }
  }

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split two tablets.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 4);
  WaitUntilSplitIsSuccesful(tablets.Get(1).tablet_id(), table, 5);

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));
  ASSERT_EQ(tablets_after_split.size(), 5);

  // Restart after the split.
  ASSERT_OK(DestroyVirtualWAL());

  int total_dml_performed = 4 * num_batches * inserts_per_batch;
  int leftover_dml_records = total_dml_performed - received_dml_records;
  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, leftover_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  GetAllPendingChangesResponse final_resp;
  vector<CDCSDKProtoRecordPB> final_records = get_consistent_changes_resp.records;
  for (const auto& record : resp.records) {
    final_records.push_back(record);
  }
  final_resp.records = final_records;
  for (int i = 0; i < 8; ++i) {
    final_resp.record_count[i] = get_consistent_changes_resp.record_count[i] + resp.record_count[i];
  }

  CheckRecordsConsistencyFromVWAL(final_resp.records);
  CheckRecordCount(final_resp, total_dml_performed);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestTabletSplitDuringConsumptionFromVWALWithRestartOnPartialAck) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 15;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 20;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 2;
  int inserts_per_batch = 30;
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)", kTableName, j, j + 1));
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  // The expected DML record is set to 35 in consideration with the flags values set above for
  // maximum records in GetConsistentChanges & GetChanges. This value is chosen such that we receive
  // partial records from a txn at the end.
  int expected_dml_records = 35;
  std::vector<CDCSDKProtoRecordPB> received_records;
  int received_dml_records = 0;
  // The record count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN,
  // COMMIT in that order.
  int record_count_before_restart[] = {0, 0, 0, 0, 0, 0, 0, 0};
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  while (received_dml_records < expected_dml_records) {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    for (const auto& record : get_consistent_changes_resp.cdc_sdk_proto_records()) {
      received_records.push_back(record);
      UpdateRecordCount(record, record_count_before_restart);
      if (IsDMLRecord(record)) {
        ++received_dml_records;
      }
    }
  }

  LOG(INFO) << "Total received DML records: " << received_dml_records;
  // We should have received only partial records from the last txn.
  ASSERT_TRUE(IsDMLRecord(received_records.back()));
  auto last_record_txn_id = received_records.back().row_message().pg_transaction_id();
  auto last_record_lsn = received_records.back().row_message().pg_lsn();
  // Send feedback for the last txn that is fully received. The partially received txn will be
  // received again on restart.
  std::vector<CDCSDKProtoRecordPB> records_to_be_received_again_on_restart;
  int dml_to_be_received_again_on_restart = 0;
  // The record count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN,
  // COMMIT in that order.
  int common_record_count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint64_t restart_lsn = 0;
  for (const auto& record : received_records) {
    if (record.row_message().pg_transaction_id() == last_record_txn_id) {
      records_to_be_received_again_on_restart.push_back(record);
      UpdateRecordCount(record, common_record_count);
      if (IsDMLRecord(record)) {
        ++dml_to_be_received_again_on_restart;
      }
    } else {
      // restart_lsn will be the (lsn + 1) of the commit record of the second last txn. we are
      // following commit_lsn + 1 feedback mechanism model.
      restart_lsn = record.row_message().pg_lsn() + 1;
    }
  }

  LOG(INFO) << "Records_to_be_received_again_on_restart: "
            << AsString(records_to_be_received_again_on_restart);

  ASSERT_OK(UpdateAndPersistLSN(stream_id, last_record_lsn, restart_lsn));
  auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id, kCDCSDKSlotEntryTabletId));
  ASSERT_EQ(result.restart_lsn, restart_lsn - 1);
  ASSERT_EQ(result.confirmed_flush_lsn, last_record_lsn);

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split two tablets.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 4);
  WaitUntilSplitIsSuccesful(tablets.Get(1).tablet_id(), table, 5);

  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, num_batches * inserts_per_batch);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (2 * num_batches * inserts_per_batch));
  });

  t1.join();
  t2.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));
  ASSERT_EQ(tablets_after_split.size(), 5);

  // Restart after the split.
  ASSERT_OK(DestroyVirtualWAL());

  int total_dml_performed = 3 * num_batches * inserts_per_batch;
  int leftover_dml_records =
      total_dml_performed - received_dml_records + dml_to_be_received_again_on_restart;
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, leftover_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";

  size_t expected_repeated_records_size = records_to_be_received_again_on_restart.size();

  //  Let x be the num of partial records received for the last txn in the previous
  // GetConsistentChanges call. Since we had partially acknowledged this txn, it will be received
  // again from the start on a restart. Therefore, The first x records received after restart should
  // match with our expected records list.
  for (size_t i = 0; i < expected_repeated_records_size; ++i) {
    AssertCDCSDKProtoRecords(
        records_to_be_received_again_on_restart[i], resp_after_restart.records[i]);
  }

  for (size_t i = expected_repeated_records_size; i < resp_after_restart.records.size(); ++i) {
    received_records.push_back(resp_after_restart.records[i]);
  }

  GetAllPendingChangesResponse final_resp;
  final_resp.records = received_records;
  for (int i = 0; i < 8; ++i) {
    final_resp.record_count[i] = record_count_before_restart[i] +
                                 resp_after_restart.record_count[i] - common_record_count[i];
  }

  CheckRecordsConsistencyFromVWAL(received_records);
  CheckRecordsConsistencyFromVWAL(resp_after_restart.records);
  CheckRecordCount(final_resp, total_dml_performed);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestChildTabletPolledFromLatestCheckpointOnVWALRestart) {
  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> p_tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &p_tablets, nullptr));
  ASSERT_EQ(p_tablets.size(), 1);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Table having key:value_1 column
  ASSERT_OK(WriteRows(0 /* start */, 10 /* end */, &test_cluster_));

  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));
  WaitUntilSplitIsSuccesful(p_tablets.Get(0).tablet_id(), table);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(
      test_client()->GetTablets(
          table, 0, &tablets_after_split, nullptr, RequireTabletsRunning::kFalse,
          master::IncludeInactive::kTrue));
  // tablets_after_split should have 3 tablets - one parent & two childrens
  ASSERT_EQ(tablets_after_split.size(), 3);

  // Stalling the updates to cdc state table for one of the child tablets
  for (const auto& tablet : tablets_after_split) {
    if (tablet.tablet_id() != p_tablets.Get(0).tablet_id()) {
      FLAGS_TEST_cdc_tablet_id_to_stall_state_table_updates = tablet.tablet_id();
      break;
    }
  }

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  // Doing 4 iterations of GetConsistentChanges:
  // - 1st call will report the tablet split error to VWAL and VWAL will successfully replace parent
  // tablet with its children tablets, creating tablet_queues for them.
  // - 2nd call's response will return records > 0 from the children tablets.
  // - 3rd and 4th calls are required to update the explicit checkpoint of tablet-stream entry in
  // cdc state table for child tablet other than
  // FLAGS_TEST_cdc_tablet_id_to_stall_state_table_updates.
  for (int i = 1; i <= 4; i++) {
    auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    if (change_resp.cdc_sdk_proto_records_size() > 0) {
      ASSERT_EQ(i, 2);  // Only 2nd call should return records.
      auto last_record = change_resp.cdc_sdk_proto_records().rbegin();
      auto last_lsn = last_record->row_message().pg_lsn();
      ASSERT_OK(UpdateAndPersistLSN(stream_id, last_lsn, last_lsn));
    }
  }

  // Restarting the VWAL. Although both the child tablets were already polled, however as we are
  // simulating the situation where only one of the child tablet got polled (since the other
  // child's tablet-stream entry in cdc state table is not updated to reflect that),
  // VWAL on restart thinks that both the child tablets are not being polled yet and so create
  // parent tablet's queue again.
  ASSERT_OK(DestroyVirtualWAL());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  // Doing 2 iterations of GetConsistentChanges:
  // - 1st one will still report the tablet split error on VWAL side (and returns zero records in
  // response).
  // - 2nd one's response shouldn't get any records as records for both child tablets were already
  // polled before the restart.
  for (int i = 1; i <= 2; i++) {
    auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  }
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestChildrenTabletsCheckpointMoveAheadOfParentTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> p_tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &p_tablets, nullptr));
  ASSERT_EQ(p_tablets.size(), 1);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  vector<uint64_t> commit_lsn;

  // Performing some INSERT transactions and polling the records so as to populate VWAL's
  // commit_meta_and_last_req_map_.
  for (int i = 1; i <= 4; i++) {
    ASSERT_OK(WriteRows(i /* start */, i + 1 /* end */, &test_cluster_));
    auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);
    auto record = change_resp.cdc_sdk_proto_records().Get(2);
    ASSERT_EQ(record.row_message().op(), RowMessage::COMMIT);
    commit_lsn.push_back(record.row_message().pg_lsn());
  }

  // Updating and persisting LSN only till 2nd txn's commit LSN.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn[1], commit_lsn[1] + 1));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  auto checkpoint_1 = ASSERT_RESULT(
      GetStreamCheckpointInCdcState(test_client(), stream_id, p_tablets[0].tablet_id()));
  ASSERT_GT(checkpoint_1, OpId(1, 1));

  // Splitting the tablet
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));
  WaitUntilSplitIsSuccesful(p_tablets.Get(0).tablet_id(), table);
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(
      test_client()->GetTablets(
          table, 0, &tablets_after_split, nullptr, RequireTabletsRunning::kFalse,
          master::IncludeInactive::kTrue));
  // tablets_after_split should have 3 tablets - one parent & two childrens
  ASSERT_EQ(tablets_after_split.size(), 3);

  // Next GetConsistentChanges() call will report the tablet split error with 0 records.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);

  // Updating and persisting LSN till 3rd txn's commit LSN. Note that this txn was streamed when
  // parent tablet hadn't split.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn[2], commit_lsn[2] + 1));

  ASSERT_OK(WriteRows(100 /* start */, 101 /* end */, &test_cluster_));
  // Next 2 GetConsistentChanges() call will return records from each child tablet.
  for (int i = 1; i <= 2; i++) {
    change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    if (change_resp.cdc_sdk_proto_records_size() > 0) {
      ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);
      auto record = change_resp.cdc_sdk_proto_records().Get(2);
      ASSERT_EQ(record.row_message().op(), RowMessage::COMMIT);
      commit_lsn.push_back(record.row_message().pg_lsn());
    }
  }

  // - The parent tablet's checkpoint will not move ahead as it isn't polled for any records after
  // the split.
  // - The checkpoint of child tablets will also not move ahead of checkpoint_1 as VWAL's
  // commit_meta_and_last_req_map_ will have empty last_sent_req_for_begin_map in its first element
  // for child tablets. And so no explicit checkpoint was sent by VWAL in previous GetChanges() call
  // for each child tablet.
  for (const auto& tablet : tablets_after_split) {
    auto checkpoint =
        ASSERT_RESULT(GetStreamCheckpointInCdcState(test_client(), stream_id, tablet.tablet_id()));
    ASSERT_EQ(checkpoint, checkpoint_1);
  }

  // Updating and persisting commit LSN of last txn.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn.back(), commit_lsn.back()));
  // Next 2 GetConsistentChanges() call will poll each child tablet.
  for (int i = 1; i <= 2; i++) {
    change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  }

  // The child tablets checkpoint should have moved ahead of checkpoint_1.
  for (const auto& tablet : tablets_after_split) {
    if (tablet.tablet_id() == p_tablets[0].tablet_id()) {
      continue;
    }
    auto checkpoint =
        ASSERT_RESULT(GetStreamCheckpointInCdcState(test_client(), stream_id, tablet.tablet_id()));
    ASSERT_GT(checkpoint, checkpoint_1);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestDynamicTablesAddition) {
  uint64_t publication_refresh_interval = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      publication_refresh_interval;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
    ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int expecpted_count[] = {0, 3, 0, 0, 0, 0, 2, 2};

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (1,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  SleepFor(MonoDelta::FromSeconds(2 * publication_refresh_interval));

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (2,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (3,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));


  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // We will receive the records from txn 1 belonging to table test1 only.
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);

  for (auto record : change_resp.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, count);
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    ASSERT_EQ(record.row_message().table(), "test1");
  }


  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // Between txn1 and txn2 a publication refresh record will be popped from the priority queue.
  // (Because of the sleep between the two txns).
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_GT(change_resp.publication_refresh_time(), 0);

  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));

  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, count);
  }
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expecpted_count[i], count[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestDynamicTablesRemoval) {
  uint64_t publication_refresh_interval = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      publication_refresh_interval;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
    ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int expecpted_count[] = {0, 3, 0, 0, 0, 0, 2, 2};

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (1,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  SleepFor(MonoDelta::FromSeconds(2 * publication_refresh_interval));

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (2,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (3,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));


  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id(), table_2.table_id()}));

  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // We will receive records from txn 2 belonging to both the txns.
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 4);
  bool resp_has_insert_from_test1 = false;
  bool resp_has_insert_from_test2 = false;
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, count);
    if (record.row_message().op() == RowMessage_Op_INSERT &&
        record.row_message().table() == "test1") {
      resp_has_insert_from_test1 = true;
    } else if (
        record.row_message().op() == RowMessage_Op_INSERT &&
        record.row_message().table() == "test2") {
      resp_has_insert_from_test2 = true;
    }
  }
  ASSERT_TRUE(resp_has_insert_from_test1 && resp_has_insert_from_test2);

  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // Between txn1 and txn2 a publication refresh record will be popped from the priority queue.
  // (Because of the sleep between the two txns).
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_GT(change_resp.publication_refresh_time(), 0);

  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id()}));

  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, count);
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    ASSERT_EQ(record.row_message().table(), "test1");
  }
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expecpted_count[i], count[i]);
  }
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestCommitTimeTieWithoutSeparateResponseForPublicationRefreshNotification) {
  TestCommitTimeTieWithPublicationRefreshRecord(false);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestCommitTimeTieWithSeparateResponseForPublicationRefreshNotification) {
  TestCommitTimeTieWithPublicationRefreshRecord(true);
}

// This method is used to test the behaviour of VWAL when the publication refresh record from the
// publication refresh tablet queue has a commit time same as one of the transactions. The expected
// behaviour in such cases is that the publication refresh record will be popped from the priority
// queue after all the other records with same commit time. There are two cases possible here : Case
// 1: Txn records are sent in the same response which has the need_pub_refresh set to true. Case 2:
// Txn records are sent in a different response then the one with needs_pub_refresh set to true. In
// both these cases when the acknowledgement feedback is received for the txn , upon restart we
// should see that we repopulate this publication refresh record and accordingly signal the
// WALSender to refresh publication's table list
void CDCSDKConsumptionConsistentChangesTest::TestCommitTimeTieWithPublicationRefreshRecord(
    bool pub_refresh_record_in_separate_response) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));

  // Create two streams. Stream 1 will be used to calculate delta and stream 2 will be used to
  // create commit time ties.
  xrepl::StreamId stream_1 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  xrepl::StreamId stream_2 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Get the Consistent Snapshot Time for stream 2.
  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_2));
  auto cdcsdk_consistent_snapshot_time = slot_row->last_pub_refresh_time;
  HybridTime commit_time;

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (2,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (3,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (4,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  // Initialize VWAL and call GetConsistentChanges to find out the commit time of the second
  // transaction.
  ASSERT_OK(InitVirtualWAL(stream_1, {table_1.table_id()}));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_1));

  uint64_t restart_lsn = 0;
  uint64_t last_record_lsn = 0;

  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 7);
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    last_record_lsn = record.row_message().pg_lsn();
    restart_lsn = last_record_lsn + 1;
    if (record.row_message().op() == RowMessage_Op_COMMIT) {
      commit_time = HybridTime(record.row_message().commit_time());
    }
  }

  // Calculate the difference between commit time and consistent snapshot time. We need to set out
  // refresh interval equal to this difference so as to create commit time ties of special record
  // with txn 2
  auto delta = commit_time.PhysicalDiff(cdcsdk_consistent_snapshot_time).ToMicroseconds();

  ASSERT_OK(DestroyVirtualWAL());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_use_microseconds_refresh_interval) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_publication_list_refresh_interval_micros) = delta;
  ASSERT_OK(InitVirtualWAL(stream_2, {table_1.table_id()}));

  if (pub_refresh_record_in_separate_response) {
    // Disable populating the safepoint records so that we can get Publication refresh notification
    // in a separate GetConsistentChanges response
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_populate_safepoint_record) = false;
  } else {
    // This transaction wont be streamed in next GetConsistentChanges call as its
    // commit time > publication refresh time
    ASSERT_OK(conn.Execute("BEGIN;"));
    ASSERT_OK(conn.Execute("INSERT INTO test1 values (5,1)"));
    ASSERT_OK(conn.Execute("INSERT INTO test2 values (6,1)"));
    ASSERT_OK(conn.Execute("COMMIT;"));
  }

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_2));
        return change_resp.cdc_sdk_proto_records_size() != 0;
      },
      MonoDelta::FromSeconds(60), "Timed out waiting for records"));

  if (pub_refresh_record_in_separate_response) {
    // We will receive 5 records in the previous GetConsistentChanges response. This is because the
    // commit of txn 2 will be popped but not shipped because the tablet queue for test1
    // will be empty as there are no safepoint records.
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 5);

    //  This transaction wont be streamed in next GetConsistentChanges call as its
    //  commit time > publication refresh time.
    ASSERT_OK(conn.Execute("BEGIN;"));
    ASSERT_OK(conn.Execute("INSERT INTO test1 values (5,1)"));
    ASSERT_OK(conn.Execute("INSERT INTO test2 values (6,1)"));
    ASSERT_OK(conn.Execute("COMMIT;"));

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_2));
          return change_resp.cdc_sdk_proto_records_size() == 1;
        },
        MonoDelta::FromSeconds(60), "Did not get commit record of Txn 2"));

    // Commit record of txn2 will be shipped when publication refresh record will be popped from the
    // priority queue.
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 1);
    auto record = change_resp.cdc_sdk_proto_records()[0];
    ASSERT_EQ(record.row_message().op(), RowMessage_Op_COMMIT);
    ASSERT_EQ(record.row_message().commit_time(), commit_time.ToUint64());
  } else {
    // With safepoints we will receive all the records in txn 1 and txn 2.
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 6);
  }

  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_EQ(commit_time.ToUint64(), change_resp.publication_refresh_time());

  ASSERT_OK(UpdatePublicationTableList(stream_2, {table_1.table_id(), table_2.table_id()}));
  ASSERT_OK(UpdateAndPersistLSN(stream_2, last_record_lsn, restart_lsn));

  // Restart
  ASSERT_OK(DestroyVirtualWAL());
  ASSERT_OK(InitVirtualWAL(stream_2, {table_1.table_id()}));

  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_2));

  // Since the txn2 was acknowledged, the pub_refresh_time corresponding to its commit record (which
  // will be equal to consistent snapshot time) will be persisted. Upon restart the first
  // publication_refresh_record will have commit time equal to the persisted last_pub_refresh_time +
  // refresh_interval. In this case this is equal to the commit time of txn 2. As a result the
  // response of first GetConsistentChanges after restart will contain no records, but will have the
  // fields to indicate publication refresh set.
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_EQ(commit_time.ToUint64(), change_resp.publication_refresh_time());
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestLSNDeterminismWithSpecialRecordOnRestartWithPartialAck) {
  uint64_t publication_refresh_interval =  500000; /* 0.5 sec */
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 15;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 20;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_use_microseconds_refresh_interval) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_publication_list_refresh_interval_micros) =
      publication_refresh_interval;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 2;
  int inserts_per_batch = 30;
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)", kTableName, j, j + 1));
    }
    ASSERT_OK(conn.Execute("COMMIT"));
    // Sleep to ensure that we will receive publication refresh record.
    SleepFor(MonoDelta::FromMicroseconds(2 * publication_refresh_interval));
  }

  // The expected DML record is set to 35 in consideration with the flags values set above for
  // maximum records in GetConsistentChanges & GetChanges. This value is chosen such that we receive
  // partial records from a txn at the end.
  int expected_dml_records = 35;
  std::vector<CDCSDKProtoRecordPB> received_records;
  int received_dml_records = 0;
  // The record count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN,
  // COMMIT in that order.
  int record_count_before_restart[] = {0, 0, 0, 0, 0, 0, 0, 0};
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  bool received_pub_refresh_indicator = false;
  while (received_dml_records < expected_dml_records) {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    if (get_consistent_changes_resp.has_needs_publication_table_list_refresh() &&
        get_consistent_changes_resp.needs_publication_table_list_refresh() &&
        get_consistent_changes_resp.has_publication_refresh_time() &&
        get_consistent_changes_resp.publication_refresh_time() > 0) {
      received_pub_refresh_indicator = true;
    }
    for (const auto& record : get_consistent_changes_resp.cdc_sdk_proto_records()) {
      received_records.push_back(record);
      UpdateRecordCount(record, record_count_before_restart);
      if (IsDMLRecord(record)) {
        ++received_dml_records;
      }
    }
  }
  ASSERT_TRUE(received_pub_refresh_indicator);

  // We should have received only partial records from the last txn.
  ASSERT_TRUE(IsDMLRecord(received_records.back()));
  auto last_record_txn_id = received_records.back().row_message().pg_transaction_id();
  auto last_record_lsn = received_records.back().row_message().pg_lsn();
  // Send feedback for the last txn that is fully received. The partially received txn will be
  // received again on restart.
  std::vector<CDCSDKProtoRecordPB> records_to_be_received_again_on_restart;
  int dml_to_be_received_again_on_restart = 0;
  // The record count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN,
  // COMMIT in that order.
  int common_record_count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint64_t restart_lsn = 0;
  for (const auto& record : received_records) {
    if (record.row_message().pg_transaction_id() == last_record_txn_id) {
      records_to_be_received_again_on_restart.push_back(record);
      UpdateRecordCount(record, common_record_count);
      if (IsDMLRecord(record)) {
        ++dml_to_be_received_again_on_restart;
      }
    } else {
      // restart_lsn will be the (lsn + 1) of the commit record of the second last txn. we are
      // following commit_lsn + 1 feedback mechanism model.
      restart_lsn = record.row_message().pg_lsn() + 1;
    }
  }

  LOG(INFO) << "records_to_be_received_again_on_restart: "
            << AsString(records_to_be_received_again_on_restart);

  ASSERT_OK(UpdateAndPersistLSN(stream_id, last_record_lsn, restart_lsn));
  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->restart_lsn, restart_lsn - 1);
  ASSERT_EQ(slot_row->confirmed_flush_lsn, last_record_lsn);

  // Sleep to ensure that we will receive publication refresh record.
  SleepFor(MonoDelta::FromMicroseconds(2 * publication_refresh_interval));
  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch));
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch));
  });

  t1.join();
  t2.join();

  ASSERT_OK(DestroyVirtualWAL());

  int total_dml_performed = 3 * num_batches * inserts_per_batch;
  int leftover_dml_records =
      total_dml_performed - received_dml_records + dml_to_be_received_again_on_restart;
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, leftover_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";
  ASSERT_TRUE(resp_after_restart.has_publication_refresh_indicator);

  size_t expected_repeated_records_size = records_to_be_received_again_on_restart.size();

  //  Let x be the num of partial records received for the last txn in the previous
  // GetConsistentChanges call. Since we had partially acknowledged this txn, it will be received
  // again from the start on a restart. Therefore, The first x records received after restart should
  // match with our expected records list.
  for (size_t i = 0; i < expected_repeated_records_size; ++i) {
    AssertCDCSDKProtoRecords(
        records_to_be_received_again_on_restart[i], resp_after_restart.records[i]);
  }

  for (size_t i = expected_repeated_records_size; i < resp_after_restart.records.size(); ++i) {
    received_records.push_back(resp_after_restart.records[i]);
  }

  GetAllPendingChangesResponse final_resp;
  final_resp.records = received_records;
  for (int i = 0; i < 8; ++i) {
    final_resp.record_count[i] = record_count_before_restart[i] +
                                 resp_after_restart.record_count[i] - common_record_count[i];
  }

  CheckRecordsConsistencyFromVWAL(received_records);
  CheckRecordsConsistencyFromVWAL(resp_after_restart.records);
  CheckRecordCount(final_resp, total_dml_performed);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestDynamicTablesAdditionForTableCreatedAfterStream) {
  auto publication_refresh_interval = MonoDelta::FromSeconds(10);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      publication_refresh_interval.ToSeconds();

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int expected_count[] = {0, 3, 0, 0, 0, 0, 2, 2};

  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));

  // Create a dynamic table.
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));

  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  vector<CDCSDKProtoRecordPB> records;
  GetConsistentChangesResponsePB change_resp;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        records.insert(
            records.end(), change_resp.cdc_sdk_proto_records().begin(),
            change_resp.cdc_sdk_proto_records().end());
        return change_resp.has_needs_publication_table_list_refresh() &&
               change_resp.needs_publication_table_list_refresh() &&
               change_resp.has_publication_refresh_time();
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));

  ASSERT_EQ(records.size(), 3);
  for (auto record : records) {
    UpdateRecordCount(record, count);
  }

  ASSERT_GT(change_resp.publication_refresh_time(), 0);

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (2,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (3,1)"));
  ASSERT_OK(conn.CommitTransaction());

  // Since we received the notification to update publication's table list, we now call
  // UpdatePublicationTableList with updated table list.
  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));

  bool has_records_from_test1 = false;
  bool has_records_from_test2 = false;
  records.clear();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        records.insert(
            records.end(), change_resp.cdc_sdk_proto_records().begin(),
            change_resp.cdc_sdk_proto_records().end());
        return records.size() >= 4;
      },
      MonoDelta::FromSeconds(60),
      "Timed out waiting to receive the records of second transaction"));

  for (auto record : records) {
    if (record.row_message().table() == "test1") {
      has_records_from_test1 = true;
    } else if (record.row_message().table() == "test2") {
      has_records_from_test2 = true;
    }
    UpdateRecordCount(record, count);
  }
  ASSERT_TRUE(has_records_from_test1 && has_records_from_test2);

  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
}

// Test that retention barriers on a tablet peer are removed only after they become stale, even if
// the stream is deleted.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestRetentionBarrierPreservedUntilStale) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_min_replicated_index_considered_stale_secs) = 6;

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Sleep for around FLAGS_cdc_min_replicated_index_considered_stale_secs * 2 seconds. Then check
  // that retention barrier are not lifted yet on this tablet as UdatePeersAndMetrics had kept
  // refreshing the tablet peer's cdc_min_replicated_index_refresh_time_.
  SleepFor(MonoDelta::FromSeconds(FLAGS_cdc_min_replicated_index_considered_stale_secs * 2));
  auto checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablet_peer->tablet_id()));
  LogRetentionBarrierAndRelatedDetails(checkpoint_result, tablet_peer);
  ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_LT(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  ASSERT_LT(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());

  // Drop the consistent snapshot stream.
  ASSERT_TRUE(DeleteCDCStream(stream_id));

  // Now sleep for FLAGS_cdc_min_replicated_index_considered_stale_secs / 2 seconds. The retention
  // barriers should still be present as they have not yet become stale.
  SleepFor(MonoDelta::FromSeconds(FLAGS_cdc_min_replicated_index_considered_stale_secs / 2));
  ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_LT(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  ASSERT_LT(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());

  // Now after around FLAGS_cdc_min_replicated_index_considered_stale_secs / 2 seconds the
  // retention barriers should become stale and be lifted.
  VerifyTransactionParticipant(tablet_peer->tablet_id(), OpId::Max());
  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_EQ(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
}

// Test that creation of dynamic table fails when setting retention barrier on its tablets fails.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestFailureSettingRetentionBarrierOnDynamicTable) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));

  ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_sdk_fail_setting_retention_barrier) = true;

  auto table_2_result = CreateTable(&test_cluster_, kNamespaceName, "test2");
  if (table_2_result.ok()) {
    auto table_2 = *table_2_result;
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table_2, 0, &tablets, nullptr));
    ASSERT_EQ(tablets.size(), 1);
    auto tablet_peer =
        ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));
    ASSERT_NE(tablet_peer->state(), tablet::RaftGroupStatePB::RUNNING);
  }
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestRetentionBarrierMovementForTablesNotInPublication) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 5;

  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table_2"));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_1;
  ASSERT_OK(test_client()->GetTablets(table_1, 0, &tablets_1, nullptr));
  ASSERT_EQ(tablets_1.size(), 1);
  auto tablet_peer_1 =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets_1.begin()->tablet_id()));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_2;
  ASSERT_OK(test_client()->GetTablets(table_2, 0, &tablets_2, nullptr));
  ASSERT_EQ(tablets_2.size(), 1);
  auto tablet_peer_2 =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets_2.begin()->tablet_id()));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Set the replica identity for both tables to FULL. This is needed for Update Peers and Metrics
  // to update safe time.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table_2 REPLICA IDENTITY FULL"));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // This simulates the situation where table_2 is not included in the publication.
  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  ASSERT_NE(tablet_peer_2->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  auto safe_time = tablet_peer_2->get_cdc_sdk_safe_time();

  ASSERT_OK(WriteRowsHelper(1, 10, &test_cluster_, true));

  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 11);
  uint64_t restart_lsn = change_resp.cdc_sdk_proto_records().Get(10).row_message().pg_lsn() + 1;

  ASSERT_OK(UpdateAndPersistLSN(stream_id, restart_lsn, restart_lsn));

  // Sleep (as per the flags value) to ensure that revision of retention barriers is not blocked.
  SleepFor(MonoDelta::FromSeconds(
      FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs * 2 * kTimeMultiplier));

  // Update Peers and Metrics should have moved forward the safe time for table_2.
  ASSERT_GT(tablet_peer_2->get_cdc_sdk_safe_time(), safe_time);

  // The safetime for both the table's tablets should be equal to slot's commit time.
  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(tablet_peer_1->get_cdc_sdk_safe_time(), slot_row->record_id_commit_time);
  ASSERT_EQ(tablet_peer_2->get_cdc_sdk_safe_time(), slot_row->record_id_commit_time);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestBeforeImageNotExistErrorPropagation) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  uint32_t num_cols = 3;
  auto table = ASSERT_RESULT(CreateTable(
    &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, false, "", "public", num_cols));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  // Set the replica identity to FULL. This is needed to get before image in update operation.
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY FULL", kTableName));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  map<std::string, uint32_t> col_val_map1, col_val_map2;
  col_val_map1.insert({"col2", 9});
  col_val_map1.insert({"col3", 10});
  col_val_map2.insert({"col2", 10});
  col_val_map2.insert({"col3", 11});
  ASSERT_OK(UpdateRowsHelper(1, 2, &test_cluster_, true, 1, col_val_map1, col_val_map2, num_cols));

  // Getting the consistent changes from stream and expecting a non-ok status due to "Failed to get
  // the beforeimage" error. This error occurs when an UPDATE follows an INSERT for the same row in
  // the same transaction, and the before image is unavailable.
  // However, when packed rows are enabled, all the UPDATE operation/s of the same row gets absorbed
  // into the INSERT operation and hence the before image is not required. In that case, we expect
  // GetConsistentChanges to return the records without any error.
  if (FLAGS_ysql_enable_packed_row) {
    ASSERT_OK(GetConsistentChangesFromCDC(stream_id));
  } else {
    ASSERT_NOK_STR_CONTAINS(
        GetConsistentChangesFromCDC(stream_id), "Failed to get the beforeimage");
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestRetryableErrorsNotSentToWalsender) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  vector<TestSimulateErrorCode> error_codes = {
      TestSimulateErrorCode::PeerNotStarted,
      TestSimulateErrorCode::TabletUnavailable,
      TestSimulateErrorCode::PeerNotLeader,
      TestSimulateErrorCode::PeerNotReadyToServe,
      TestSimulateErrorCode::LogSegmentFooterNotFound,
      TestSimulateErrorCode::LogIndexCacheEntryNotFound};

  for (auto error_code : error_codes) {
    // Setting the flag to mimic retryable errors. The expectation is that
    // CDCServiceImpl::GetConsistentChanges() should not return an error since such error is
    // expected to be retryable.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_simulate_error_for_get_changes) = error_code;
    auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_FALSE(change_resp.has_error());
  }
}

// This test verifies the behaviour of VWAL when the publication refresh interval is changed when
// consumption is in progress.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestChangingPublicationRefreshInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test2"));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Publication refresh intervals in seconds.
  uint64_t pub_refresh_intervals[] = {5, 3, 2, 1};
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      pub_refresh_intervals[0];
  vector<uint64_t> pub_refresh_times;

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (1,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  uint64_t confirmed_flush_lsn = 0;
  uint64_t restart_lsn = 0;

  // First GCC call. We will receive the records corresponding to the txn 1 for table 1 only. We
  // will also receive the first publication refresh time.
  vector<CDCSDKProtoRecordPB> records;
  GetConsistentChangesResponsePB change_resp;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records.insert(
              records.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
          confirmed_flush_lsn =
              change_resp.cdc_sdk_proto_records().Get(2).row_message().pg_lsn() + 1;
          restart_lsn = confirmed_flush_lsn;
        }
        return change_resp.has_needs_publication_table_list_refresh() &&
               change_resp.needs_publication_table_list_refresh() &&
               change_resp.has_publication_refresh_time();
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));
  ASSERT_EQ(records.size(), 3);

  // Perform txn 2 after popping pub_refresh_record 1 from PQ.
  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (2,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (2,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  // Store the publication refresh time for comparison after restart.
  pub_refresh_times.push_back(change_resp.publication_refresh_time());

  // Verify the value of pub_refresh_times in the state table.
  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->pub_refresh_times, GetPubRefreshTimesString(pub_refresh_times));

  // Update the publication's table list and change the refresh interval.
  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      pub_refresh_intervals[1];

  // Second GCC call. We will receive the records for txn 2 for both the tables. We
  // will also receive the second publication refresh time.
  records.clear();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        records.insert(
            records.end(), change_resp.cdc_sdk_proto_records().begin(),
            change_resp.cdc_sdk_proto_records().end());
        return change_resp.has_needs_publication_table_list_refresh() &&
               change_resp.needs_publication_table_list_refresh() &&
               change_resp.has_publication_refresh_time();
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));
  ASSERT_EQ(records.size(), 4);

  // Perform txn 3 after popping pub_refresh_record 2 from PQ.
  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (3,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (3,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  pub_refresh_times.push_back(change_resp.publication_refresh_time());

  // Verify that the second pub_refresh_time = first pub_refresh_time + changed refresh interval.
  auto ht = HybridTime(pub_refresh_times[0]);
  ht = ht.AddSeconds(pub_refresh_intervals[1]);
  ASSERT_EQ(change_resp.publication_refresh_time(), ht.ToUint64());

  // Verify the value of pub_refresh_times in the state table.
  slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->pub_refresh_times, GetPubRefreshTimesString(pub_refresh_times));

  // Update the publication's table list and change the refresh interval.
  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id()}));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      pub_refresh_intervals[2];

  // Third GCC call. We will receive the records for txn 3 for table 1 only. We
  // will also receive the third publication refresh time.
  records.clear();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        records.insert(
            records.end(), change_resp.cdc_sdk_proto_records().begin(),
            change_resp.cdc_sdk_proto_records().end());
        return change_resp.has_needs_publication_table_list_refresh() &&
               change_resp.needs_publication_table_list_refresh() &&
               change_resp.has_publication_refresh_time();
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));

  ASSERT_EQ(records.size(), 3);
  pub_refresh_times.push_back(change_resp.publication_refresh_time());

  // Verify that the  pub_refresh_time = previous pub_refresh_time + changed refresh interval.
  ht = HybridTime(pub_refresh_times[1]);
  ht = ht.AddSeconds(pub_refresh_intervals[2]);
  ASSERT_EQ(change_resp.publication_refresh_time(), ht.ToUint64());

  // Verify the value of pub_refresh_times in the state table.
  slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->pub_refresh_times, GetPubRefreshTimesString(pub_refresh_times));

  // Send acknowledgemetn for txn 1. This should not trim the pub_refresh_times list. Upon restart
  // the very first record to be popped should be a publication refresh record.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn));

  // Restart VWAL with new refresh interval.
  ASSERT_OK(DestroyVirtualWAL());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      pub_refresh_intervals[3];
  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  // Since we had acknowledged txn 1, the first record to be popped will be a publication refresh
  // record. Hence we will get an empty response with a signal to refresh publication list.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_EQ(change_resp.publication_refresh_time(), pub_refresh_times[0]);
  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));

  // In this GCC call, we will receive the records of txn 2 for both the tables. Similar to second
  // GCC call.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 4);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_EQ(change_resp.publication_refresh_time(), pub_refresh_times[1]);

  // Send acknowledgement for txn 2. This will trim the pub_refresh_times list, as the first
  // pub_refresh_time is no longer of use.
  confirmed_flush_lsn = change_resp.cdc_sdk_proto_records().Get(3).row_message().pg_lsn() + 1;
  restart_lsn = confirmed_flush_lsn;
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn));
  pub_refresh_times.erase(pub_refresh_times.begin());

  // Verify that the pub_refresh_times has been trimmed in state table slot entry.
  slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->pub_refresh_times, GetPubRefreshTimesString(pub_refresh_times));

  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id()}));

  // In this GCC call we will receive the records of txn 3 for one table only. Similar to third GCC
  // call.
  records.clear();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records.insert(
              records.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }
        return change_resp.has_needs_publication_table_list_refresh() &&
               change_resp.needs_publication_table_list_refresh() &&
               change_resp.has_publication_refresh_time();
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));

  ASSERT_EQ(records.size(), 3);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_EQ(change_resp.publication_refresh_time(), pub_refresh_times[1]);

  // Verify the value of pub_refresh_times in the state table.
  slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->pub_refresh_times, GetPubRefreshTimesString(pub_refresh_times));
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestLSNDeterminismWithChangingPubRefreshInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) = 30;

  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test2"));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (1,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  SleepFor(MonoDelta::FromSeconds(5));

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (2,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (2,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  vector<CDCSDKProtoRecordPB> records_before_restart;
  GetConsistentChangesResponsePB change_resp;
  bool contains_pub_refresh = false;

  // Call GCC and consume records for both the txns.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records_before_restart.insert(
              records_before_restart.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }
        contains_pub_refresh =
            contains_pub_refresh || (change_resp.has_needs_publication_table_list_refresh() &&
                                     change_resp.needs_publication_table_list_refresh() &&
                                     change_resp.has_publication_refresh_time());
        return records_before_restart.size() == 6;
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));

  // The first pub_refresh_record will have a commit time of consistent snapshot time + 30 secs, and
  // hence will be popped out after the two txns.
  ASSERT_FALSE(contains_pub_refresh);

  // Restart with a new smaller pub refresh interval.
  ASSERT_OK(DestroyVirtualWAL());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) = 1;
  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  vector<CDCSDKProtoRecordPB> records_after_restart;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records_after_restart.insert(
              records_after_restart.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }
        contains_pub_refresh =
            contains_pub_refresh || (change_resp.has_needs_publication_table_list_refresh() &&
                                     change_resp.needs_publication_table_list_refresh() &&
                                     change_resp.has_publication_refresh_time());

        // We will receive the records from both the txns as per the table list provided in
        // Init Virtual WAL.
        return records_after_restart.size() == 6;
      },
      MonoDelta::FromSeconds(60), "Timed out waiting to receive the records"));

  ASSERT_FALSE(contains_pub_refresh);
  ASSERT_EQ(records_before_restart.size(), records_after_restart.size());
  for (size_t i = 0; i < records_after_restart.size(); i++) {
    AssertCDCSDKProtoRecords(records_before_restart[i], records_after_restart[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestDynamicTablesSwitch) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_enable_dynamic_table_support) = false;

  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test2"));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Txn 1.
  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (1,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  vector<CDCSDKProtoRecordPB> records_before_restart;
  GetConsistentChangesResponsePB change_resp;
  bool contains_pub_refresh = false;

  // Call GetConsistentChanges to consume the records of first txn.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records_before_restart.insert(
              records_before_restart.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }
        contains_pub_refresh =
            contains_pub_refresh || (change_resp.has_needs_publication_table_list_refresh() &&
                                     change_resp.needs_publication_table_list_refresh() &&
                                     change_resp.has_publication_refresh_time());
        return records_before_restart.size() == 3;
      },
      MonoDelta::FromSeconds(20 * kTimeMultiplier), "Timed out waiting to receive the records"));

  // This will be false since the flag cdcsdk_enable_dynamic_table_support is false.
  ASSERT_FALSE(contains_pub_refresh);

  // Verify slot entry.
  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_EQ(slot_row->pub_refresh_times, "");
  ASSERT_NE(slot_row->last_decided_pub_refresh_time, "");
  ASSERT_EQ(slot_row->last_decided_pub_refresh_time.back(), 'F');

  // Txn 2
  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (2,2)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (2,2)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  // Turn dynamic tables support ON.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_enable_dynamic_table_support) = true;

  // Call GetConsistentChanges till we receive a pub refresh notification.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records_before_restart.insert(
              records_before_restart.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }
        contains_pub_refresh =
            contains_pub_refresh || (change_resp.has_needs_publication_table_list_refresh() &&
                                     change_resp.needs_publication_table_list_refresh() &&
                                     change_resp.has_publication_refresh_time());
        return contains_pub_refresh;
      },
      MonoDelta::FromSeconds(20 * kTimeMultiplier), "Timed out waiting to receive the records"));

  // Verfiy slot entry.
  slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_NE(slot_row->pub_refresh_times, "");
  ASSERT_NE(slot_row->last_decided_pub_refresh_time, "");
  ASSERT_EQ(slot_row->last_decided_pub_refresh_time.back(), 'T');

  // Update the tables list.
  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));

  // Txn 3.
  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (3,3)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (3,3)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  // Turn dynamic tables support OFF.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_enable_dynamic_table_support) = false;
  contains_pub_refresh = false;

  // Sleep to ensure that a pub refresh record is popped from the PQ between txn 3 and txn 4.
  SleepFor(MonoDelta::FromSeconds(
      FLAGS_cdcsdk_publication_list_refresh_interval_secs * kTimeMultiplier));

  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 values (4,4)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 values (4,4)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  // Call GetConsistentChanges to consume txn 3 and txn 4. No pub refresh notification should be
  // received as the flag cdcsdk_enable_dynamic_table_support is FALSE.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records_before_restart.insert(
              records_before_restart.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }

        contains_pub_refresh =
            contains_pub_refresh || (change_resp.has_needs_publication_table_list_refresh() &&
                                     change_resp.needs_publication_table_list_refresh() &&
                                     change_resp.has_publication_refresh_time());

        return records_before_restart.size() == 14;
      },
      MonoDelta::FromSeconds(20 * kTimeMultiplier), "Timed out waiting to receive the records"));

  ASSERT_FALSE(contains_pub_refresh);

  // Verify slot entry.
  slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_NE(slot_row->pub_refresh_times, "");
  ASSERT_NE(slot_row->last_decided_pub_refresh_time, "");
  ASSERT_EQ(slot_row->last_decided_pub_refresh_time.back(), 'F');

  // Restart.
  ASSERT_OK(DestroyVirtualWAL());
  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  vector<CDCSDKProtoRecordPB> records_after_restart;

  // Call GetConsistentChanges till we consume records of all 4 txns. Update Publication's tables
  // list if a pub refresh notification is received.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
        if (change_resp.cdc_sdk_proto_records_size() > 0) {
          records_after_restart.insert(
              records_after_restart.end(), change_resp.cdc_sdk_proto_records().begin(),
              change_resp.cdc_sdk_proto_records().end());
        }
        contains_pub_refresh =
            contains_pub_refresh || (change_resp.has_needs_publication_table_list_refresh() &&
                                     change_resp.needs_publication_table_list_refresh() &&
                                     change_resp.has_publication_refresh_time());

        if (contains_pub_refresh) {
          RETURN_NOT_OK(
              UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));
        }
        return records_after_restart.size() == 14;
      },
      MonoDelta::FromSeconds(20 * kTimeMultiplier), "Timed out waiting to receive the records"));

  // Check for LSN determinism.
  ASSERT_EQ(records_before_restart.size(), records_after_restart.size());
  for (size_t i = 0; i < records_after_restart.size(); i++) {
    AssertCDCSDKProtoRecords(records_before_restart[i], records_after_restart[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestConsumptionAfterDroppingTableNotInPublication) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 20;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int primary key, value_1 int) SPLIT INTO 3 TABLETS"));
  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table_1_tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table_2_tablets;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &table_1_tablets, nullptr));
  ASSERT_EQ(table_1_tablets.size(), 3);
  ASSERT_OK(test_client()->GetTablets(table2, 0, &table_2_tablets, nullptr));
  ASSERT_EQ(table_2_tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  // insert 200 records in both tables.
  int dml_records_per_table = 200;
  for (int i = 0; i < dml_records_per_table; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", i, i + 1));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1)", i, i + 1));
  }

  std::thread t1([&]() -> void {
    // Drop test2 which is not part of the publication.
    LOG(INFO) << "Dropping table: " << table2.table_name();
    DropTable(&test_cluster_, table2.table_name().c_str());
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          while (true) {
            auto get_resp = GetDBStreamInfo(stream_id);
            // Wait until the background thread cleanup up the drop table metadata.
            if (get_resp.ok() && !get_resp->has_error() && get_resp->table_info_size() == 1) {
              return true;
            }
            SleepFor(MonoDelta::FromSeconds(2));
          }
        },
        MonoDelta::FromSeconds(30), "Waiting for stream metadata cleanup."));

    // Verify state table entries for tablets of test2 are also removed.
    std::unordered_set<TabletId> expected_tablets;
    expected_tablets.insert(kCDCSDKSlotEntryTabletId);
    for (auto& entry : table_1_tablets) {
      expected_tablets.insert(entry.tablet_id());
    }
    auto cdc_state_table = MakeCDCStateTable(test_client());
    Status s;
    auto table_range =
        ASSERT_RESULT(cdc_state_table.GetTableRange(CDCStateTableEntrySelector().IncludeAll(), &s));

    bool seen_slot_entry = false;
    std::unordered_set<TabletId> tablets_found;
    for (auto row_result : table_range) {
      ASSERT_OK(row_result);
      auto& row = *row_result;
      tablets_found.insert(row.key.tablet_id);
      if (row.key.stream_id == stream_id && row.key.tablet_id == kCDCSDKSlotEntryTabletId) {
        seen_slot_entry = true;
      }
    }
    ASSERT_OK(s);
    ASSERT_TRUE(seen_slot_entry);
    LOG(INFO) << "tablets found: " << AsString(tablets_found)
              << ", expected tablets: " << AsString(expected_tablets);
    ASSERT_EQ(tablets_found, expected_tablets);
  });

  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table1.table_id()}, dml_records_per_table, true /* init_virtual_wal */,
      kVWALSessionId1));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 1.";

  auto last_record =
      get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];

  t1.join();
  // This will check the slot's entry as well as its fields.
  VerifyLastRecordAndProgressOnSlot(stream_id, last_record);
  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, dml_records_per_table);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestSlotRowDeletionWithSingleStream) {
  TestSlotRowDeletion(false);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestSlotRowDeletionWithMultipleStreams) {
  TestSlotRowDeletion(true);
}

void CDCSDKConsumptionConsistentChangesTest::TestSlotRowDeletion(bool multiple_streams) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_min_replicated_index_considered_stale_secs) =
      15 * kTimeMultiplier;

  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));

  // Set the replica identity to full, so that cdc_sdk_safe_time will be set.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  if (multiple_streams) {
    ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  }

  ASSERT_OK(WriteRowsWithConn(1, 2, &test_cluster_, &conn));

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_GE(change_resp.cdc_sdk_proto_records_size(), 3);

  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_TRUE(slot_row.has_value());

  ASSERT_TRUE(DeleteCDCStream(stream_id));

  // The slot row will be deleted from the state table.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto slot_row = VERIFY_RESULT(ReadSlotEntryFromStateTable(stream_id));
        return !slot_row.has_value();
      },
      MonoDelta::FromSeconds(10 * kTimeMultiplier),
      "Timed out waiting for slot entry deletion from state table"));

  // Verify that the retention barriers are not lifted.
  ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_NE(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());
  ASSERT_NE(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);

  // If there are no active streams for a tablet, its retention barriers will be lifted (as part of
  // ResetStaleRetentionBarriersOp op) once the exising barriers become stale. So, waiting till
  // ResetStaleRetentionBarriersOp op is eligible to lift off the retention barriers.
  SleepFor(MonoDelta::FromSeconds(FLAGS_cdc_min_replicated_index_considered_stale_secs));

  if (multiple_streams) {
    // Since one stream still exists, the retention barriers will not be lifted.
    ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
    ASSERT_NE(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());
    ASSERT_NE(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  } else {
    // Since the only stream that existed is now deleted, the retention barriers will be unset.
    VerifyTransactionParticipant(tablet_peer->tablet_id(), OpId::Max());
    ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
    ASSERT_EQ(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  }
}

// Test to verify that the replication slot row is deleted from cdc state table
// when first the stream is deleted and subsequently a table associated with the stream is dropped.
// This essentially tests that the stream's state doesn't get over-written incorrectly from
// SysCDCStreamEntryPB::DELETING (marked as part of stream deletion) to
// SysCDCStreamEntryPB::DELETING_METADATA (marked as part of table drop).
// If the state gets incorrectly overwritten before the CatalogManager's background tasks
// (corresponding for each state) run, the slot row won't be deleted.
TEST_F(CDCSDKConsumptionConsistentChangesTest, CheckSlotRowDeletionForStreamAndTableDeletion) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;

  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto tablet_id = tablets[0].tablet_id();
  auto stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  ASSERT_OK(WriteRowsHelper(0 /* start */, 1 /* end */, &test_cluster_, true));

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);

  auto slot_row = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  ASSERT_TRUE(slot_row.has_value());

  SyncPoint::GetInstance()->LoadDependency(
      {{"DeleteTableInternal::End", "RunXReplBgTasks::Start"}});
  SyncPoint::GetInstance()->EnableProcessing();

  // Before the next iteration of catalog manager background tasks, we delete the stream and table.
  ASSERT_TRUE(DeleteCDCStream(stream_id));
  DropTable(&test_cluster_, kTableName);

  // Waiting till catalog manager's background task deletes the cdc state table entries associated
  // with the stream.
  VerifyStreamDeletedFromCdcState(test_client(), stream_id, tablet_id, 30);
  VerifyStreamDeletedFromCdcState(test_client(), stream_id, kCDCSDKSlotEntryTabletId, 30);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALConsumptionWhileUpdatingNonExistingRow) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  // Update a row that does not exist in table. We expect to not receive any record corresponding to
  // this update. Although, we will receive a DDL record from cdc_service but that DDL doesnt have a
  // commit_time, therefore will be filtered out by the VWAL.
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 10 WHERE key = 50"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test_table ($0, $1) select i, i+1 from generate_series(1,10) as i",
      kKeyColumnName, kValueColumnName));

  // 10 insert records should be received.
  int expected_dml_records = 10;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyFromVWAL(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCreationOfSlotOnNewDBAfterUpgrade) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  // Keep the flags for consistent snapshot streams and replication slot consumption disabled,
  // simulating a cluster before upgrade.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_consumption) = false;

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int expected_count_old_model[] = {2, 2, 0, 1, 0, 0, 3, 3};
  int expected_count_new_model[] = {0, 1, 0, 0, 0, 0, 1, 1};
  int actual_count_old_model[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int actual_count_new_model[] = {0, 0, 0, 0, 0, 0, 0, 0};

  ASSERT_OK(SetUpWithParams(1, 1, false, true));

  // Create a table and old model stream.
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  auto conn_1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table_1, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id_1 =
      ASSERT_RESULT((CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::ALL)));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_1, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // Enable the flags for consistent snapshot streams and replication slot consumption, simulating
  // an upgrade.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_consumption) = true;

  // Create a new DB and a replication slot stream.
  std::string kNamespaceName_2 = "upgraded_test_namespace";
  ASSERT_OK(CreateDatabase(&test_cluster_, kNamespaceName_2));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName_2, "t1"));
  auto conn_2 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName_2));
  xrepl::StreamId stream_id_2 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot(
      "test_slot", CDCSDKSnapshotOption::EXPORT_SNAPSHOT, false, kNamespaceName_2));

  auto dynamic_table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table_2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> dynamic_tablets_1;
  ASSERT_OK(test_client()->GetTablets(dynamic_table_1, 0, &dynamic_tablets_1, nullptr));
  ASSERT_EQ(dynamic_tablets_1.size(), 1);

  // A dynamically created table in the first DB should have invalid safetime and OpId.
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), dynamic_tablets_1.begin()->tablet_id()));
  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_EQ(tablet_peer->cdc_sdk_min_checkpoint_op_id() , OpId::Invalid());

  auto dynamic_table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName_2, "t2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> dynamic_tablets_2;
  ASSERT_OK(test_client()->GetTablets(dynamic_table_2, 0, &dynamic_tablets_2, nullptr));
  ASSERT_EQ(dynamic_tablets_2.size(), 1);

  // A dynamically created table in the second DB should not have invalid OpId. Its safe time will
  // be invalid as its replica identity is CHANGE.
  tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), dynamic_tablets_2.begin()->tablet_id()));
  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_NE(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Invalid());

  set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_1, dynamic_tablets_1, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // Insert into tables from older DB.
  ASSERT_OK(WriteRows(1, 2, &test_cluster_));
  ASSERT_OK(conn_1.Execute("INSERT INTO test_table_2 VALUES (1,1)"));
  ASSERT_OK(conn_1.Execute("DELETE FROM test_table_2 WHERE key = 1"));

  // Insert into tables from new DB.
  ASSERT_OK(conn_2.Execute("INSERT INTO t1 values (1,1)"));

  // Call GetChanges on test_table from older DB.
  auto change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets));
  // 1 DDL + 1 BEGIN + 1 INSERT + 1 COMMIT + 1 SAFEPOINT
  ASSERT_EQ(change_resp_1.cdc_sdk_proto_records().size(), 5);
  for (auto record : change_resp_1.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, actual_count_old_model);
  }

  // Call GetChanges on dynamic table from older DB.
  change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id_1, dynamic_tablets_1));
  // 2 BEGIN + 1 INSERT + 1 DELETE + 2 COMMIT + 2 SAFEPOINT
  ASSERT_EQ(change_resp_1.cdc_sdk_proto_records().size(), 8);
  for (auto record : change_resp_1.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, actual_count_old_model);
    // The dynamic table from Old DB should have Before Image record type ALL.
    if (record.row_message().op() == RowMessage_Op_DELETE) {
      ASSERT_EQ(record.row_message().old_tuple_size(), 2);
      for (auto old_tuple : record.row_message().old_tuple()) {
        ASSERT_EQ(old_tuple.datum_int32(), 1);
      }
    }
  }

  auto change_resp_2 = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id_2, {table_2.table_id()}, 1, true /* init_virtual_wal */));
  // 1 BEGIN + 1 INSERT + 1 COMMIT
  ASSERT_EQ(change_resp_2.records.size(), 3);
  for (auto record : change_resp_2.records) {
    UpdateRecordCount(record, actual_count_new_model);
  }

  for (int i = 0; i < 8 ; i++ ) {
    ASSERT_EQ(expected_count_old_model[i], actual_count_old_model[i]);
    ASSERT_EQ(expected_count_new_model[i], actual_count_new_model[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCreationOfOldStreamAfterUpgrade) {
  // All the flags are ON by default, this simulates an upgraded environment.
  ASSERT_OK(SetUpWithParams(1, 1, false, true));

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int expected_count_old_model[] = {1, 1, 0, 0, 0, 0, 1, 1};
  int expected_count_new_model[] = {0, 1, 0, 0, 0, 0, 1, 1};
  int actual_count_old_model[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int actual_count_new_model[] = {0, 0, 0, 0, 0, 0, 0, 0};

  // Create a table and a replication slot.
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));
  auto conn_1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto stream_id_1 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(conn_1.Execute("INSERT INTO test1 values (1,1)"));

  // Create a new DB and an old consumption model stream.
  std::string kNamespaceName_2 = "test_namespace_2";
  ASSERT_OK(CreateDatabase(&test_cluster_, kNamespaceName_2));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName_2, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table_2, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id_2 = ASSERT_RESULT(
      (CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::CHANGE, kNamespaceName_2)));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_2, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  auto conn_2 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName_2));
  ASSERT_OK(conn_2.Execute("INSERT INTO test2 values (2,2)"));

  // Call GetConsistentChanges on table_1.
  auto change_resp_1 = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id_1, {table_1.table_id()}, 1, true /* init_virtual_wal */));
  ASSERT_EQ(change_resp_1.records.size(), 3);
  for (auto record : change_resp_1.records) {
    UpdateRecordCount(record, actual_count_new_model);
  }

  // Call GetChanges on table_2.
  auto change_resp_2 = ASSERT_RESULT(GetChangesFromCDC(stream_id_2, tablets));
  ASSERT_EQ(change_resp_2.cdc_sdk_proto_records().size(), 5);
  for (auto record : change_resp_2.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, actual_count_old_model);
  }

  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count_old_model[i], actual_count_old_model[i]);
    ASSERT_EQ(expected_count_new_model[i], actual_count_new_model[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestFailureCreatingStreamsOfDifferentTypesOnSameDB) {
  ASSERT_OK(SetUpWithParams(1, 1, false, true));

  std::string kNamespaceName_2 = "test_namespace_for_old_model";
  ASSERT_OK(CreateDatabase(&test_cluster_, kNamespaceName_2));

  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));
  auto table_2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName_2, "test2"));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_1;
  ASSERT_OK(test_client()->GetTablets(table_1, 0, &tablets_1, nullptr));
  ASSERT_EQ(tablets_1.size(), 1);
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_2;
  ASSERT_OK(test_client()->GetTablets(table_2, 0, &tablets_2, nullptr));
  ASSERT_EQ(tablets_2.size(), 1);

  ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot(
      "test_slot", CDCSDKSnapshotOption::EXPORT_SNAPSHOT, false /*verify_snapshot_name*/,
      kNamespaceName));
  // Since a replication slot is created on kNamespace, creation of old model stream should fail.
  ASSERT_NOK(CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::CHANGE, kNamespaceName));

  ASSERT_RESULT(
      CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::CHANGE, kNamespaceName_2));
  // Since a old model stream is created on kNamespace_2, creation of replication slot should fail.
  ASSERT_NOK(CreateConsistentSnapshotStreamWithReplicationSlot(
      "test_slot_2", CDCSDKSnapshotOption::EXPORT_SNAPSHOT, false /*verify_snapshot_name*/,
      kNamespaceName_2));
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/23096.
TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestPgReplicationSlotsWithReplicationCommandsDisabled) {
  ASSERT_OK(SetUpWithParams(
      /*replication_factor=*/3, /*num_masters=*/1, /*colocated=*/false));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // View must work when the commands are disabled.
  auto slots = ASSERT_RESULT(conn.Fetch("SELECT * FROM pg_replication_slots"));
  ASSERT_EQ(PQntuples(slots.get()), 0);

  // Enable so that we can create the replication slot. It isn't sufficient to just set the GFlag
  // value since PG GUC values are passed on from the Tserver at the postmaster creation. So we need
  // to also set the GUC value directly using the `SET` statement.
  ASSERT_OK(conn.Execute("SET yb_enable_replication_commands TO true"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot("pg_replication_slots_without_replcmds"));

  // Disable again and ensure that the view returns an empty response.
  ASSERT_OK(conn.Execute("SET yb_enable_replication_commands TO false"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;
  slots = ASSERT_RESULT(conn.Fetch("SELECT * FROM pg_replication_slots"));
  ASSERT_EQ(PQntuples(slots.get()), 0);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestStreamExpiry) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test1 (id int PRIMARY KEY, value_1 int) SPLIT INTO 3 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 10;
  int inserts_per_batch = 100;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  int expected_dml_records = num_batches * inserts_per_batch;
  auto vwal1_result = GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */);

  ASSERT_NOK(vwal1_result);
  ASSERT_TRUE(vwal1_result.status().IsInternalError());
  ASSERT_STR_CONTAINS(vwal1_result.status().message().ToBuffer(), "expired for Tablet");

  // A new VWAL on the same stream should again receive the stream expired error.
  auto vwal2_result = GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      kVWALSessionId2);

  ASSERT_NOK(vwal2_result);
  ASSERT_TRUE(vwal2_result.status().IsInternalError());
  ASSERT_STR_CONTAINS(vwal2_result.status().message().ToBuffer(), "expired for Tablet");
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestIntentGC) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_skip_stream_active_check) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int PRIMARY KEY, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  int num_batches = 50;
  int inserts_per_batch = 2;
  for (int i = 0; i < num_batches; i++) {
    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = i * inserts_per_batch; j < ((i + 1) * inserts_per_batch); j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1)", j, j + 1));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
  }

  // Sleep for UpdatePeersAndMetrics to move retention barriers that will lead to garbage collection
  // of intents.
  SleepFor(MonoDelta::FromSeconds(30 * kTimeMultiplier));
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  bool received_gc_error = false;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto vwal1_result = GetConsistentChangesFromCDC(stream_id);

        if (!vwal1_result.ok()) {
          if (vwal1_result.status().IsInternalError() &&
              vwal1_result.status().message().ToBuffer().find(
                  "CDCSDK Trying to fetch already GCed intents") != std::string::npos) {
            received_gc_error = true;
            return true;
          }
        }

        return false;
      },
      MonoDelta::FromSeconds(60), "Did not see Intents GC error"));

  ASSERT_TRUE(received_gc_error);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestColocationWithIndexes) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_use_byte_threshold_for_vwal_changes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 20;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  ASSERT_OK(SetUpWithParams(
      1, 1, true /* colocated */, true /* cdc_populate_safepoint_record */,
      true /* set_pgsql_proxy_bind_address */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // This array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int expected_count[] = {0, 501, 0, 0, 0, 0, 2, 2};

  // Create a table.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(key int primary key, value_1 int)", kTableName));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));

  // Create an index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0_idx ON $0(value_1 ASC)", kTableName));

  // Create a consistent snapshot stream.
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Perform 1 multi-shard txn and 1 single shard txn.
  ASSERT_OK(WriteRowsHelperWithConn(0, 500, &test_cluster_, true, &conn));
  ASSERT_OK(WriteRowsWithConn(500, 501, &test_cluster_, &conn));

  auto checkpoint_before = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));

  // We should receive DMLs corresponding to the above txns only. No records corresponding to the
  // index table should be seen. Since we have reduced the batch size, GetChanges will be called
  // multiple times.
  auto gcc_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, 501, true /* init_virtual_wal */));

  ASSERT_EQ(gcc_resp.records.size(), 505);
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], gcc_resp.record_count[i]);
  }

  // Assert that the checkpoint has moved forward.
  auto checkpoint_after = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));
  ASSERT_EQ(checkpoint_before.size(), 1);
  ASSERT_EQ(checkpoint_after.size(), 1);
  ASSERT_GT(checkpoint_after[0].index, checkpoint_before[0].index);

  // This GetConsistentChanges call will fetch an empty batch. The response will only contain
  // safepoint records.
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    ASSERT_EQ(record.row_message().op(), RowMessage_Op_SAFEPOINT);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCompactionWithReplicaIdentityDefault) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(
      /* replication_factor */ 3, /* num_masters */ 1, /* colocated */ false,
      /* cdc_populate_safepoint_record */ true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY DEFAULT"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  auto expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_NE((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);

  // Assert that the safe time is not invalid in the tablet_peers
  AssertSafeTimeAsExpectedInTabletPeers(tablets[0].tablet_id(), (*expected_row).cdc_sdk_safe_time);

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId1));
  int expected_dml_records = 1;
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */,
      kVWALSessionId1));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";
  // (BEGIN + DML + COMMIT )
  ASSERT_EQ(get_consistent_changes_resp.records.size(), 3);

  // Additional call to send explicit checkpoint
  auto resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, kVWALSessionId1));
  ASSERT_EQ(resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(UpdateRows(1 /* key */, 6 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 10 /* value */, &test_cluster_));
  LOG(INFO) << "Sleep for UpdatePeersAndMetrics to move barriers";
  SleepFor(MonoDelta::FromSeconds(2 * FLAGS_update_min_cdc_indices_interval_secs));
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  int count_after_compaction;
  ASSERT_NOK(WaitFor(
      [&]() {
        auto result = test_cluster_.mini_cluster_->CompactTablets();
        if (!result.ok()) {
          return false;
        }
        count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
        if (count_after_compaction < count_before_compaction) {
          return true;
        }
        return false;
      },
      MonoDelta::FromSeconds(15), "Compaction is restricted for the stream."));
  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  ASSERT_EQ(count_after_compaction, count_before_compaction);

  expected_dml_records = 2;
  get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */,
      kVWALSessionId1));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";
  // 2 * (BEGIN + DML + COMMIT )
  ASSERT_EQ(get_consistent_changes_resp.records.size(), 6);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestHiddenTabletDeletionOfUnpolledTable) {
  auto parent_tablet_deletion_task_interval = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) =
      parent_tablet_deletion_task_interval;
  ASSERT_OK(SetUpWithParams(
      3 /* replication_factor */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  const uint32_t num_tablets = 1;
  const uint32_t num_tables = 2;
  vector<string> table_suffix = {"_1", "_2"};
  vector<YBTableName> tables(num_tables);
  vector<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> tablets(num_tables);

  // Create two tables, each having one tablet.
  for (uint32_t idx = 0; idx < num_tables; idx++) {
    tables[idx] = ASSERT_RESULT(
        CreateTable(&test_cluster_, kNamespaceName, kTableName + table_suffix[idx], num_tablets));
    ASSERT_OK(test_client()->GetTablets(
        tables[idx], 0, &tablets[idx], nullptr /* partition_list_version */));
    ASSERT_EQ(tablets[idx].size(), num_tablets);
  }

  // Create a slot, and start a VWAL instance for test_table_1 only.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {tables[0].table_id()}, kVWALSessionId1));

  // Get the state table entry of the tablet for test_table_2.
  auto parent_tablet_row =
      ASSERT_RESULT(ReadFromCdcStateTable(stream_id, tablets[1].Get(0).tablet_id()));

  // Write 100 rows to test_table_2 and then split it.
  ASSERT_OK(
      WriteRowsHelper(0, 100, &test_cluster_, true, 2, (kTableName + table_suffix[1]).c_str()));
  ASSERT_OK(WaitForFlushTables({tables[1].table_id()}, false, 1000, true));
  WaitUntilSplitIsSuccesful(tablets[1].Get(0).tablet_id(), tables[1], 2);

  // Sleep to ensure that parent tablet deletion task has run for atleast one iteration.
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier * parent_tablet_deletion_task_interval));

  // Get all the tablets including hidden tablets for test_table_2. We should get 3 tablets, i.e. 2
  // children and 1 un-deleted hidden parent tablet, since the restart time is behind the tablet
  // split time.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> get_tablets_res;
  ASSERT_OK(test_client()->GetTablets(
      tables[1], 3, &get_tablets_res, nullptr, RequireTabletsRunning::kFalse,
      master::IncludeInactive::kTrue));
  ASSERT_EQ(get_tablets_res.size(), 3);

  // Now, we insert and consume records from test_table_1. This will move the restart time forward
  // and eventually it will become greater than the tablet split time. As a result the hidden parent
  // tablet will be deleted.
  auto i = 0;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        RETURN_NOT_OK(WriteRowsHelper(
            i, i + 100, &test_cluster_, true, 2, (kTableName + table_suffix[0]).c_str()));
        auto get_consistent_changes_resp = VERIFY_RESULT(GetAllPendingTxnsFromVirtualWAL(
            stream_id, {tables[0].table_id()}, 100 /* expected_dml_records*/,
            false /* init_virtual_wal */));
        get_tablets_res.Clear();
        auto s = test_client()->GetTablets(
            tables[1], 3, &get_tablets_res, nullptr, RequireTabletsRunning::kFalse,
            master::IncludeInactive::kTrue);
        i += 100;
        SleepFor(MonoDelta::FromSeconds(5));
        return get_tablets_res.size() == 2;
      },
      MonoDelta::FromSeconds(120 * kTimeMultiplier),
      "Timed out waiting for hidden tablet deletion"));

  // Assert that children tablet entries have the parent entry's checkpoint.
  for (auto child_tablet : get_tablets_res) {
    auto child_tablet_row =
        ASSERT_RESULT(ReadFromCdcStateTable(stream_id, child_tablet.tablet_id()));
    ASSERT_EQ(parent_tablet_row.op_id, child_tablet_row.op_id);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestHiddenTabletDeletionWithUnusedSlot) {
  auto parent_tablet_deletion_task_interval = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) =
      parent_tablet_deletion_task_interval;

  ASSERT_OK(SetUpWithParams(
      3 /* replication_factor */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  // Create one table with single tablet.
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr /* partition_list_version */));
  ASSERT_EQ(tablets.size(), 1);

  // Create 2 slots, only one of them will be used for polling.
  auto polled_stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  auto unpolled_stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Get the state table entries of the parent tablet.
  auto parent_tablet_row_polled_stream =
      ASSERT_RESULT(ReadFromCdcStateTable(polled_stream_id, tablets.Get(0).tablet_id()));
  auto parent_tablet_row_unpolled_stream =
      ASSERT_RESULT(ReadFromCdcStateTable(unpolled_stream_id, tablets.Get(0).tablet_id()));

  ASSERT_OK(WriteRowsHelper(0, 10, &test_cluster_, true));

  // Split the tablet.
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 2);

  // Consume the 10 inserted records with the first slot.
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      polled_stream_id, {table.table_id()}, 10 /* expected_dml_records*/,
      true /* init_virtual_wal */));

  // Sleep to ensure that parent tablet deletion task has run for atleast one iteration.
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier * parent_tablet_deletion_task_interval));

  // Get all the tablets including hidden tablets. We should get 3 tablets, i.e 2 children and 1
  // un-deleted hidden parent tablet, since one slot is unpolled leaving restart time across all
  // slots behind the tablet split time.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> get_tablets_res;
  ASSERT_OK(test_client()->GetTablets(
      table, 3, &get_tablets_res, nullptr, RequireTabletsRunning::kFalse,
      master::IncludeInactive::kTrue));
  ASSERT_EQ(get_tablets_res.size(), 3);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 0;

  // Now, since cdc_wal_retention_time_secs has been set to zero, we will delete the hidden parent
  // tablet as now cdc_wal_retention_time_secs interval has passed since the tablet split time.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        get_tablets_res.Clear();
        auto s = test_client()->GetTablets(
            table, 3, &get_tablets_res, nullptr, RequireTabletsRunning::kFalse,
            master::IncludeInactive::kTrue);

        return get_tablets_res.size() == 2;
      },
      MonoDelta::FromSeconds(120 * kTimeMultiplier),
      "Timed out waiting for hidden tablet deletion"));

  // Assert that children tablet entries have the parent entry's checkpoint.
  for (auto child_tablet : get_tablets_res) {
    auto child_tablet_row_polled_stream =
        ASSERT_RESULT(ReadFromCdcStateTable(polled_stream_id, child_tablet.tablet_id()));
    auto child_tablet_row_unpolled_stream =
        ASSERT_RESULT(ReadFromCdcStateTable(unpolled_stream_id, child_tablet.tablet_id()));

    ASSERT_EQ(parent_tablet_row_polled_stream.op_id, child_tablet_row_polled_stream.op_id);
    ASSERT_EQ(parent_tablet_row_unpolled_stream.op_id, child_tablet_row_unpolled_stream.op_id);
  }
}

// This test verifies that it is safe to add a table to polling list (publication) after its tablets
// have been split and hidden tablets deleted.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestDynamicTablesAdditionAfterHiddenTabletDeletion) {
  uint64_t publication_refresh_interval = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      publication_refresh_interval;

  ASSERT_OK(SetUpWithParams(
      3 /* replication_factor */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  const uint32_t num_tablets = 1;
  const uint32_t num_tables = 2;
  vector<string> table_suffix = {"_1", "_2"};
  vector<YBTableName> tables(num_tables);
  vector<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> tablets(num_tables);

  // Create two tables, each having one tablet.
  for (uint32_t idx = 0; idx < num_tables; idx++) {
    tables[idx] = ASSERT_RESULT(
        CreateTable(&test_cluster_, kNamespaceName, kTableName + table_suffix[idx], num_tablets));
    ASSERT_OK(test_client()->GetTablets(
        tables[idx], 0, &tablets[idx], nullptr /* partition_list_version */));
    ASSERT_EQ(tablets[idx].size(), num_tablets);
  }

  // Create a slot.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Get the state table entry of the tablet for test_table_2.
  auto parent_tablet_row =
      ASSERT_RESULT(ReadFromCdcStateTable(stream_id, tablets[1].Get(0).tablet_id()));

  // Write 100 rows to test_table_2 and then split it.
  ASSERT_OK(
      WriteRowsHelper(0, 100, &test_cluster_, true, 2, (kTableName + table_suffix[1]).c_str()));
  ASSERT_OK(WaitForFlushTables({tables[1].table_id()}, false, 1000, true));
  WaitUntilSplitIsSuccesful(tablets[1].Get(0).tablet_id(), tables[1], 2);

  // Get all the tablets including hidden tablets for test_table_2. We should get 3 tablets, i.e. 2
  // children and 1 un-deleted hidden parent tablet, since the restart time is behind the tablet
  // split time.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> get_tablets_res;
  ASSERT_OK(test_client()->GetTablets(
      tables[1], 3, &get_tablets_res, nullptr, RequireTabletsRunning::kFalse,
      master::IncludeInactive::kTrue));
  ASSERT_EQ(get_tablets_res.size(), 3);

  // Init Virtual Wal with only test_table_1.
  ASSERT_OK(InitVirtualWAL(stream_id, {tables[0].table_id()}, kVWALSessionId1));

  // Insert and consume records from test_table_1 so that its restart time moves ahead, leading to
  // the deletion of hidden tablet belonging to test_table_2. Once the hidden tablet has been
  // deleted, add test_table_2 to the polling list.
  auto i = 0;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        RETURN_NOT_OK(WriteRowsHelper(
            i, i + 100, &test_cluster_, true, 2, (kTableName + table_suffix[0]).c_str()));
        auto get_consistent_changes_resp = VERIFY_RESULT(GetAllPendingTxnsFromVirtualWAL(
            stream_id, {tables[0].table_id()}, 100 /* expected_dml_records*/,
            false /* init_virtual_wal */));
        get_tablets_res.Clear();
        auto s = test_client()->GetTablets(
            tables[1], 3, &get_tablets_res, nullptr, RequireTabletsRunning::kFalse,
            master::IncludeInactive::kTrue);
        i += 100;
        SleepFor(MonoDelta::FromSeconds(5));
        return get_tablets_res.size() == 2;
      },
      MonoDelta::FromSeconds(120 * kTimeMultiplier),
      "Timed out waiting for hidden tablet deletion"));

  // Assert that children tablet entries have the parent entry's checkpoint.
  for (auto child_tablet : get_tablets_res) {
    auto child_tablet_row =
        ASSERT_RESULT(ReadFromCdcStateTable(stream_id, child_tablet.tablet_id()));
    ASSERT_EQ(parent_tablet_row.op_id, child_tablet_row.op_id);
  }

  // Sleep to ensure that we receive a pub refresh record.
  SleepFor(MonoDelta::FromSeconds(2 * publication_refresh_interval));

  // Perform a txn inserting a record each in both the tables.
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN;"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table_1 values (9999999,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table_2 values (9999999,1)"));
  ASSERT_OK(conn.Execute("COMMIT;"));

  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_GT(change_resp.publication_refresh_time(), 0);

  // Update the publication's tables list.
  ASSERT_OK(UpdatePublicationTableList(stream_id, {tables[0].table_id(), tables[1].table_id()}));

  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {} /* table_ids */, 2 /* expected_dml_records*/, false /* init_virtual_wal */));

  // We will receive the records from the above txn belonging to both the tables
  ASSERT_EQ(
      get_consistent_changes_resp.records[1].row_message().table(), kTableName + table_suffix[0]);
  ASSERT_EQ(
      get_consistent_changes_resp.records[2].row_message().table(), kTableName + table_suffix[1]);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestFlushLagMetricWithRestartTimeMovement) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_metrics_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  // Create a table with 1 tablet.
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr /* partition_list_version =*/));
  ASSERT_EQ(tablets.size(), num_tablets);

  // Create a slot and InitVWAL.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId1));

  const auto& tserver = test_cluster()->mini_tablet_server(0)->server();
  auto cdc_service = CDCService(tserver);
  ASSERT_OK(WaitFor(
      [&]() { return cdc_service->CDCEnabled(); }, MonoDelta::FromSeconds(30), "IsCDCEnabled"));
  auto metrics =
      ASSERT_RESULT(GetCDCSDKTabletMetrics(*cdc_service, tablets[0].tablet_id(), stream_id));

  // As there is nothing to stream, the flush lag should be zero.
  ASSERT_EQ(metrics->cdcsdk_flush_lag->value(), 0);

  // Insert 10 records.
  ASSERT_OK(WriteRowsHelper(0, 10, &test_cluster_, true));

  // Since we have unstreamed data, the flush lag should increase.
  uint64_t prev_flush_lag = 0;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        prev_flush_lag = metrics->cdcsdk_flush_lag->value();
        return prev_flush_lag > 0;
      },
      MonoDelta::FromSeconds(30 * kTimeMultiplier), "Timed out waiting for flush lag to rise"));

  // Call GetConsistentChanges and consume the data but do not send feedback.
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 12);
  const auto confirmed_flush_lsn =
      change_resp.cdc_sdk_proto_records().Get(11).row_message().pg_lsn() + 1;
  const auto restart_lsn = confirmed_flush_lsn;

  // Since the restart time hasn't moved, the flush lag value should not decrease.
  ASSERT_GE(metrics->cdcsdk_flush_lag->value(), prev_flush_lag);

  // Provide feedback by calling UpdateAndPersistLSN. This should move the restart time forward.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn));

  // Verify that the flush lag goes down after feedback.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return metrics->cdcsdk_flush_lag->value() < prev_flush_lag; },
      MonoDelta::FromSeconds(30 * kTimeMultiplier),
      "Timed out waiting for flush lag to come down"));
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest, TestReplicationWithHashRangeConstraintsAndTabletSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_consistent_replication_from_hash_range) = true;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 2));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  auto stream_id1 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  auto stream_id2 = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  int num_batches = 5;
  int inserts_per_batch = 50;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  // Split tablet-1.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table, 3);

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));
  ASSERT_EQ(tablets_after_split.size(), 3);
  std::unique_ptr<ReplicationSlotHashRange> slot_hash_range_1 =
      std::make_unique<ReplicationSlotHashRange>(0, 32768);
  std::unique_ptr<ReplicationSlotHashRange> slot_hash_range_2 =
      std::make_unique<ReplicationSlotHashRange>(32768, 65536);

  ASSERT_OK(InitVirtualWAL(
      stream_id1, {table.table_id()}, kVWALSessionId1, std::move(slot_hash_range_1)));
  ASSERT_OK(InitVirtualWAL(
      stream_id2, {table.table_id()}, kVWALSessionId2, std::move(slot_hash_range_2)));

  GetAllPendingChangesResponse slot_resp_1;
  GetAllPendingChangesResponse slot_resp_2;
  int expected_dml_records = 4 * num_batches * inserts_per_batch;
  int received_dml_records = 0;
  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  int record_count_slot1[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int record_count_slot2[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  vector<int> dml_indexes = {1, 2, 3, 5};
  while (received_dml_records != expected_dml_records) {
    received_dml_records = 0;
    auto change_resp1 = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id1, kVWALSessionId1));
    for (int i = 0; i < change_resp1.cdc_sdk_proto_records_size(); i++) {
      slot_resp_1.records.push_back(change_resp1.cdc_sdk_proto_records(i));
      UpdateRecordCount(change_resp1.cdc_sdk_proto_records(i), record_count_slot1);
    }

    auto change_resp2 = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id2, kVWALSessionId2));
    for (int i = 0; i < change_resp2.cdc_sdk_proto_records_size(); i++) {
      slot_resp_2.records.push_back(change_resp2.cdc_sdk_proto_records(i));
      UpdateRecordCount(change_resp2.cdc_sdk_proto_records(i), record_count_slot2);
    }
    // INSERT + UPDATE + DELETE + TRUNCATE
    for (const auto& i : dml_indexes) {
      received_dml_records += (record_count_slot1[i] + record_count_slot2[i]);
    }
    LOG(INFO) << "Dml records received: " << received_dml_records;
  }

  CheckRecordsConsistencyFromVWAL(slot_resp_1.records);
  CheckRecordsConsistencyFromVWAL(slot_resp_2.records);
}


TEST_F(CDCSDKConsumptionConsistentChangesTest, TestMovingRestartTimeForwardWhenNothingToStream) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 0;
  ASSERT_OK(SetUpWithParams(
      3 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  // Create a table with 1 tablet.
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr /* partition_list_version */));
  ASSERT_EQ(tablets.size(), num_tablets);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  auto slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  auto restart_time_1 = slot_entry->record_id_commit_time;

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, kVWALSessionId1));

  //  We sleep for 5 seconds to ensure that leader safe time has moved beyond consistent snapshot
  //  time.
  SleepFor(MonoDelta::FromSeconds(5));

  // Call GetConsistentChanges before inserting any record. This represents the case where logical
  // replication is set up but no workload is active.
  // We need to call GetConsistentChanges twice as the first one will return a safepoint record with
  // commit time equal to consistent snapshot time, which is what we store in the slot entry's
  // restart time field at the time of slot creation.
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // Assert that restart time has moved forward without any workload.
  slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  auto restart_time_2 = slot_entry->record_id_commit_time;
  ASSERT_GT(restart_time_2, restart_time_1);

  // Insert 1 record, consume and acknowledge it. This should also move restart time forward.
  ASSERT_OK(WriteRows(0, 1, &test_cluster_));
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);

  auto commit_lsn = change_resp.cdc_sdk_proto_records().Get(2).row_message().pg_lsn();
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn, commit_lsn));

  slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  auto restart_time_3 = slot_entry->record_id_commit_time;
  ASSERT_GT(restart_time_3, restart_time_2);

  // Call GetConsistentChanges once again. We have already consumed all the records. This
  // represents the scenario where logical replication has consumed all the records, and no new
  // DMLs are coming in. The restart time should be moved forward in this case.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  auto restart_time_4 = slot_entry->record_id_commit_time;
  ASSERT_GT(restart_time_4, restart_time_3);

  // Increase the value of the flag cdcsdk_update_restart_time_interval_secs back to 60 seconds.
  // Since we updated the restart time in last GetConsistentChanges call, it should not be updated
  // for the next minute.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 60;
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  auto restart_time_5 = slot_entry->record_id_commit_time;
  ASSERT_EQ(restart_time_5, restart_time_4);
}

// This test verifies that we do not ship records with commit time < vwal_safe_time.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALSafeTimeWithDynamicTableAddition) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) = 30;
  ASSERT_OK(SetUpWithParams(
      3 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table_2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int expecpted_count[] = {0, 2, 0, 0, 0, 0, 2, 2};

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  ASSERT_OK(InitVirtualWAL(stream_id, {table_1.table_id()}));

  // Insert 1 record each in test1 and test2. Record inserted in test1 will be streamed. However,
  // record inserted in test2 will not be streamed since this record is committed before test2 is
  // added to the streaming tables list.
  ASSERT_OK(conn.Execute("INSERT INTO test1 VALUES (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test2 VALUES (2,2)"));

  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, count);
    // Assert that all the records belong to test1 table. Skip the BEGIN / COMMIT records from this
    // assertion as they don't contain table name.
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    ASSERT_EQ(record.row_message().table(), "test1");
  }

  // Call GetConsistentChanges once more. This call will move the Virtual WAL safe time forward.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);

  // Sleep for pub refresh interval.
  SleepFor(MonoDelta::FromSeconds(30 * kTimeMultiplier));

  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_GT(change_resp.publication_refresh_time(), 0);

  // Add test2 to the polling list.
  ASSERT_OK(UpdatePublicationTableList(stream_id, {table_1.table_id(), table_2.table_id()}));

  // Insert one more record to test2.
  ASSERT_OK(conn.Execute("INSERT INTO test2 VALUES (3,3)"));

  // Get all the pending records from VWAL. We should not receive the record inserted before test2
  // was added to the polling list.
  auto resp =
      ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(stream_id, {} /* table_ids */, 1, false));
  ASSERT_EQ(resp.records.size(), 3);
  for (auto record : resp.records) {
    UpdateRecordCount(record, count);
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }

    ASSERT_EQ(record.row_message().table(), "test2");
    if (record.row_message().op() == RowMessage_Op_INSERT) {
      // Assert that the insert is the one with id = 3.
      ASSERT_EQ(record.row_message().new_tuple()[0].pg_ql_value().int32_value(), 3);
    }
  }

  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expecpted_count[i], count[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestBlockDropTableWhenPartOfPublication) {
  ASSERT_OK(SetUpWithParams(3 /* rf */, 1 /* num_masters */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_1 (id int primary key)"));
  ASSERT_OK(conn.Execute("CREATE TABLE test_2 (id int primary key)"));

  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub_1 FOR TABLE test_1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub_2 FOR ALL TABLES"));

  // We should be able to drop the table test_2 as it is only part of pub_2 which is an ALL TABLES
  // publication.
  ASSERT_OK(conn.Execute("DROP TABLE test_2"));

  // Attempt to drop table test_1 should fail since it is also part of pub_1.
  ASSERT_NOK(conn.Execute("DROP TABLE test_1"));

  // Drop the table test_1 from pub_1. Now the drop table should succeed.
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub_1 DROP TABLE test_1"));
  ASSERT_OK(conn.Execute("DROP TABLE test_1"));
}

// This test was added as a part of #27052.
void CDCSDKConsumptionConsistentChangesTest::TestColocatedUpdateWithIndex(bool use_pk_as_index) {
  // Disable packed rows, else updates affecting all the non-key columns come as INSERTS.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ASSERT_OK(SetUpWithParams(
      1, 1, true /* colocated */, true /* cdc_populate_safepoint_record */,
      true /* set_pgsql_proxy_bind_address */));

  auto table = ASSERT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, kTableName, 1 /* num_tablets */, true /* add_pk */,
      true /* colocated */));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets,
      /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (1,1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (2,2)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (3,3)"));

  if (use_pk_as_index) {
    ASSERT_OK(conn.Execute("CREATE INDEX idx ON test_table(key)"));
  } else {
    ASSERT_OK(conn.Execute("CREATE INDEX idx ON test_table(key)"));
  }

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  ASSERT_OK(conn.Execute("UPDATE test_table set value_1 = 10 WHERE key in (1,2,3)"));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 5);
  ASSERT_EQ(change_resp.cdc_sdk_proto_records()[1].row_message().op(), RowMessage_Op_UPDATE);
  ASSERT_EQ(change_resp.cdc_sdk_proto_records()[2].row_message().op(), RowMessage_Op_UPDATE);
  ASSERT_EQ(change_resp.cdc_sdk_proto_records()[3].row_message().op(), RowMessage_Op_UPDATE);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestColocatedUpdateWithIndexOnNonPKColumn) {
  TestColocatedUpdateWithIndex(false /* use_pk_as_index*/);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestColocatedUpdateWithIndexOnPKColumn) {
  TestColocatedUpdateWithIndex(true /* use_pk_as_index*/);
}

// This test was added as a part of #27052.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestColocatedUpdateWithIndexMultiColumnTable) {
  ASSERT_OK(SetUpWithParams(
      1, 1, true /* colocated */, true /* cdc_populate_safepoint_record */,
      true /* set_pgsql_proxy_bind_address */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (id int primary key, v1 text, v2 text, v3 text, v4 int)"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(conn.Execute("INSERT INTO test_table values (1, 'abc', 'abc', 'abc', 10)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (2, 'abc', 'abc', 'abc', 11)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (3, 'abc', 'abc', 'abc', 12)"));

  ASSERT_OK(conn.Execute("CREATE INDEX idx ON test_table(v4)"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  ASSERT_OK(conn.Execute("UPDATE test_table set v1 = 'def', v4 = 100 WHERE id in (1,2)"));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 4);
  auto update_record_1 = change_resp.cdc_sdk_proto_records()[1];
  ASSERT_EQ(update_record_1.row_message().op(), RowMessage_Op_UPDATE);
  ASSERT_EQ(update_record_1.row_message().new_tuple()[0].pg_ql_value().int32_value(), 1);
  ASSERT_EQ(update_record_1.row_message().new_tuple()[1].pg_ql_value().string_value(), "def");
  ASSERT_EQ(update_record_1.row_message().new_tuple()[2].pg_ql_value().int32_value(), 100);

  auto update_record_2 = change_resp.cdc_sdk_proto_records()[2];
  ASSERT_EQ(update_record_2.row_message().op(), RowMessage_Op_UPDATE);
  ASSERT_EQ(update_record_2.row_message().new_tuple()[0].pg_ql_value().int32_value(), 2);
  ASSERT_EQ(update_record_2.row_message().new_tuple()[1].pg_ql_value().string_value(), "def");
  ASSERT_EQ(update_record_2.row_message().new_tuple()[2].pg_ql_value().int32_value(), 100);
}

void CDCSDKConsumptionConsistentChangesTest::TestColocatedUpdateAffectingNoRows(
    bool use_multi_shard) {
  ASSERT_OK(SetUpWithParams(1, 1, true /* colocated */, true /* cdc_populate_safepoint_record */));
  auto table = ASSERT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, kTableName, 1 /* num_tablets */, true /* add_pk */,
      true /* colocated */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Create a dummy_table so that multiple BEGINS and COMMITS are added for each txn.
  ASSERT_OK(conn.Execute("CREATE TABLE dummy_table (id int primary key)"));

  // Create a stream.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Insert and delete a row and then try to update it. This UPDATE will create WAL op with zero
  // write pairs. We should not receive any record  (including any BEGIN / COMMIT) corresponding to
  // this UPDATE.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (2, 2)"));
  ASSERT_OK(conn.Execute("DELETE FROM test_table WHERE key = 2"));
  if (use_multi_shard) {
    ASSERT_OK(conn.Execute("BEGIN"));
    ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 60 WHERE key = 2"));
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 60 WHERE key = 2"));
  }

  // Insert another row.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (100, 100)"));

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN and COMMIT
  // in that order.
  const int expected_count[] = {0, 2, 0, 1, 0, 0, 3, 3};
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // 3 from first INSERT, 3 from DELETE, 0 from UPDATE and 3 from second INSERT.
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 9);
  for (auto record : change_resp.cdc_sdk_proto_records()) {
    UpdateRecordCount(record, count);
  }
  for (auto i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestColocatedSingleShardUpdateAffectingNoRows) {
  TestColocatedUpdateAffectingNoRows(false /* use_multi_shard*/);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestColocatedMultiShardUpdateAffectingNoRows) {
  TestColocatedUpdateAffectingNoRows(true /* use_multi_shard*/);
}

void CDCSDKConsumptionConsistentChangesTest::TestExplcictCheckpointMovementAfterDDL(
    bool no_activity_post_ddl) {
  // We do not want the mechanism to move restart time forward when nothing is left to stream to
  // interfere with this test. We will enable this mechanism only for no_activity_post_ddl case.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  google::SetVLOGLevel("cdcsdk_virtual_wal", 3);
  ASSERT_OK(SetUpWithParams(
      3 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  auto table =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1 /* num_tablets*/));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (1, 1)"));

  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);
  auto restart_lsn = change_resp.cdc_sdk_proto_records()[2].row_message().pg_lsn();
  ASSERT_OK(UpdateAndPersistLSN(stream_id, restart_lsn, restart_lsn));

  // The explicit checkpoint will be persisted in the next GetChanges call after
  // UpdateAndPersistLSN.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  OpId old_checkpoint, new_checkpoint;
  old_checkpoint = ASSERT_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));

  // Perform a DDL.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_2 int"));

  if (no_activity_post_ddl) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 0;

    // Sleep to ensure that leader safe time moves forward.
    SleepFor(MonoDelta::FromSeconds(10 * kTimeMultiplier));

    // Keep calling GetConsistentChanges. Eventually the restart time will be moved beoynd the DDL's
    // commit time based on the SAFEPOINT records. After this we will move the checkpoint forward.
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
          new_checkpoint =
              VERIFY_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));
          return new_checkpoint.index > old_checkpoint.index;
        },
        MonoDelta::FromSeconds(120), "Timed out waiting for checkpoint to move forward"));
  } else {
    // Insert and consume a DML after the DDL.
    ASSERT_OK(conn.Execute("INSERT INTO test_table values (2, 2, 2)"));

    change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 4);

    // Since we haven't acknowledged anything after getting the old_checkpoint, the checkpoints in
    // the state table should not move.
    new_checkpoint = ASSERT_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));
    ASSERT_EQ(old_checkpoint.term, new_checkpoint.term);
    ASSERT_EQ(old_checkpoint.index, new_checkpoint.index);

    // Acknowledge the DML and call GetConsistentChanges that will persist the updated explicit
    // checkpoint.
    restart_lsn = change_resp.cdc_sdk_proto_records()[3].row_message().pg_lsn();
    ASSERT_OK(UpdateAndPersistLSN(stream_id, restart_lsn, restart_lsn));
    change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

    new_checkpoint = ASSERT_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));
  }

  ASSERT_GT(new_checkpoint.index, old_checkpoint.index);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestExplcictCheckpointMovementAfterDDLWithNoActivityPostDDL) {
  TestExplcictCheckpointMovementAfterDDL(true /* no_activity_post_ddl*/);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestExplcictCheckpointMovementAfterDDLWithActivityPostDDL) {
  TestExplcictCheckpointMovementAfterDDL(false /* no_activity_post_ddl*/);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestExplcictCheckpointMovementAfterMultipleDDL) {
  // We do not want the mechanism to move restart time forward when nothing is left to stream to
  // interfere with this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  ASSERT_OK(SetUpWithParams(
      3 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  auto table =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1 /* num_tablets*/));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  auto old_checkpoint =
      ASSERT_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Perform DDL 1.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_2 int"));

  // Insert row 1 and consume it.
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1,1,1)"));
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 4);
  auto commit_lsn_1 = change_resp.cdc_sdk_proto_records()[3].row_message().pg_lsn();

  // Perform DDL 2
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_3 int"));

  // Insert row 2 and consume it.
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2,2,2,2)"));
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 4);
  auto commit_lsn_2 = change_resp.cdc_sdk_proto_records()[3].row_message().pg_lsn();

  // Acknowledge row 1 and call GetConsistentChanges.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn_1, commit_lsn_1));
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // Since we have an unacknowledged DDL (DDL 2), we will not move the checkpoint forward.
  auto new_checkpoint =
      ASSERT_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));
  ASSERT_EQ(old_checkpoint.term, new_checkpoint.term);
  ASSERT_EQ(old_checkpoint.index, new_checkpoint.index);

  // Acknowledge row 2 and call GetConsistentChanges to send explicit checkpoint.
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn_2, commit_lsn_2));
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));

  // Now that all the DDLs have been acknowledged, we should move the checkpoint forward.
  new_checkpoint = ASSERT_RESULT(GetCheckpointFromStateTable(stream_id, tablets[0].tablet_id()));
  ASSERT_GT(new_checkpoint.index, old_checkpoint.index);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCWithSavePoint) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_savepoint_rollback_filtering) = true;

  ASSERT_OK(SetUpWithParams(
    1 /* rf */, 1 /* num_masters */, false /* colocated */,
    true /* cdc_populate_safepoint_record */));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr /* partition_list_version */));
  ASSERT_EQ(tablets.size(), num_tablets);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (1, 1)"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (2, 2)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (3, 3)"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (2, 2)"));
  ASSERT_OK(conn.Execute("END"));

  int expected_dml_records = 2;
  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */));

  // We should get BEGIN + INSERT (1,1) + INSERT (2,2) + COMMIT = 4 records.
  ASSERT_EQ(resp.records.size(), 4);
  CheckRecordsConsistencyFromVWAL(resp.records);

  // Track transaction IDs to verify new transactions are started after ROLLBACK AND CHAIN.
  ASSERT_EQ(resp.records[0].row_message().op(), RowMessage::BEGIN);
  uint64_t prev_txn_id = resp.records[0].row_message().pg_transaction_id();

  // Multiple rollback to savepoints.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (3, 3)"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp2"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (4, 4)"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp3"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (5, 5)"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp3"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (5, 5)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (6, 6)"));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT sp2"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (5, 5)"));
  ASSERT_OK(conn.Execute("END"));

  expected_dml_records = 2;
  resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */));

  // We should get BEGIN + INSERT (3,3) + INSERT (5,5) + COMMIT = 4 records.
  ASSERT_EQ(resp.records.size(), 4);
  CheckRecordsConsistencyFromVWAL(resp.records);

  // Verify new transaction has a different (greater) transaction ID.
  ASSERT_EQ(resp.records[0].row_message().op(), RowMessage::BEGIN);
  uint64_t current_txn_id = resp.records[0].row_message().pg_transaction_id();
  ASSERT_GT(current_txn_id, prev_txn_id);
  prev_txn_id = current_txn_id;

  // No rows present post rollback to savepoint.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (6, 6)"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("END"));

  // We should get BEGIN + COMMIT (0 DMLs, all rolled back).
  ASSERT_OK(WaitFor(
    [&]() -> Result<bool> {
      auto change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
      return change_resp.cdc_sdk_proto_records_size() == 2;
    },
    MonoDelta::FromSeconds(10), "Expected 2 records (BEGIN + COMMIT)"));

  // The entire transaction is rolled back, so CDC should see no data records.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Fetch("SELECT * FROM test_table"));
  ASSERT_OK(conn.Execute("SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 10 WHERE key = 1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (7, 7)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 20 WHERE key = 2"));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Fetch("SELECT * FROM test_table"));
  ASSERT_OK(conn.Execute("SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Fetch("SELECT * FROM test_table"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Execute("ROLLBACK AND CHAIN"));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // Start a new transaction with 4 inserts that actually get committed.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (7, 7)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (8, 8)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (9, 9)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (10, 10)"));
  ASSERT_OK(conn.Execute("END"));

  // The first transaction (with savepoints) was rolled back, so no records from it.
  // The second transaction with 4 inserts was committed.
  expected_dml_records = 4;
  resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */));

  // We expect BEGIN + 4 INSERTs + COMMIT = 6 records.
  ASSERT_EQ(resp.records.size(), 6);
  CheckRecordsConsistencyFromVWAL(resp.records);

  // Verify new transaction has a different (greater) transaction ID.
  ASSERT_EQ(resp.records[0].row_message().op(), RowMessage::BEGIN);
  current_txn_id = resp.records[0].row_message().pg_transaction_id();
  ASSERT_GT(current_txn_id, prev_txn_id);
  prev_txn_id = current_txn_id;

  // Test scenario: ROLLBACK AND CHAIN starts a new transaction, perform DMLs and commit.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 100 WHERE key = 1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (11, 11)"));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Execute("SAVEPOINT active_record_2"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT active_record_2"));

  // Capture distributed txn ID before ROLLBACK AND CHAIN.
  auto yb_txn_id_before_rollback =
      ASSERT_RESULT(conn.FetchRow<std::string>("SELECT yb_get_current_transaction()::text"));
  ASSERT_OK(conn.Execute("ROLLBACK AND CHAIN"));

  // Now in new transaction started by ROLLBACK AND CHAIN.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (11, 11)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (12, 12)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (13, 13)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (14, 14)"));

  // Capture distributed txn ID after ROLLBACK AND CHAIN.
  auto yb_txn_id_after_rollback =
      ASSERT_RESULT(conn.FetchRow<std::string>("SELECT yb_get_current_transaction()::text"));

  // Verify ROLLBACK AND CHAIN started a new transaction (different distributed txn ID).
  ASSERT_NE(yb_txn_id_before_rollback, yb_txn_id_after_rollback);
  ASSERT_OK(conn.Execute("DELETE FROM test_table WHERE key = 11"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 200 WHERE key = 12"));
  ASSERT_OK(conn.Execute("END"));

  // The first transaction was rolled back by ROLLBACK AND CHAIN.
  // GetAllPendingTxnsFromVirtualWAL also validates all DMLs have the same txn ID as BEGIN.
  expected_dml_records = 6;  // 4 INSERTs + 1 DELETE + 1 UPDATE
  resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */));

  // BEGIN + 4 INSERTs + 1 DELETE + 1 UPDATE + COMMIT = 8 records.
  ASSERT_EQ(resp.records.size(), 8);
  CheckRecordsConsistencyFromVWAL(resp.records);

  // Verify new transaction has a greater txn_id than all previously committed transactions.
  ASSERT_EQ(resp.records[0].row_message().op(), RowMessage::BEGIN);
  current_txn_id = resp.records[0].row_message().pg_transaction_id();
  ASSERT_GT(current_txn_id, prev_txn_id);
  prev_txn_id = current_txn_id;

  // Test scenario: DMLs between ROLLBACK TO SAVEPOINT and ROLLBACK AND CHAIN.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (15, 15)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (16, 16)"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp1"));

  // DMLs after ROLLBACK TO SAVEPOINT but before ROLLBACK AND CHAIN.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (17, 17)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 300 WHERE key = 12"));

  // Capture distributed txn ID before ROLLBACK AND CHAIN.
  yb_txn_id_before_rollback =
      ASSERT_RESULT(conn.FetchRow<std::string>("SELECT yb_get_current_transaction()::text"));
  ASSERT_OK(conn.Execute("ROLLBACK AND CHAIN"));

  // Now in new transaction - these DMLs will be committed.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (15, 15)"));

  // Capture distributed txn ID after ROLLBACK AND CHAIN.
  yb_txn_id_after_rollback =
      ASSERT_RESULT(conn.FetchRow<std::string>("SELECT yb_get_current_transaction()::text"));

  // Verify ROLLBACK AND CHAIN started a new transaction (different distributed txn ID).
  ASSERT_NE(yb_txn_id_before_rollback, yb_txn_id_after_rollback);
  ASSERT_OK(conn.Execute("DELETE FROM test_table WHERE key = 14"));
  ASSERT_OK(conn.Execute("END"));

  // The first transaction was rolled back.
  // GetAllPendingTxnsFromVirtualWAL also validates all DMLs have the same txn ID as BEGIN.
  expected_dml_records = 2;  // 1 INSERT + 1 DELETE
  resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */));

  // BEGIN + 1 INSERT + 1 DELETE + COMMIT = 4 records.
  ASSERT_EQ(resp.records.size(), 4);
  CheckRecordsConsistencyFromVWAL(resp.records);

  // Verify new transaction has a greater txn_id than all previously committed transactions.
  ASSERT_EQ(resp.records[0].row_message().op(), RowMessage::BEGIN);
  current_txn_id = resp.records[0].row_message().pg_transaction_id();
  ASSERT_GT(current_txn_id, prev_txn_id);

  // Test scenario: Same as above but ROLLBACK instead of END.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (16, 16)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (17, 17)"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp1"));
  // DMLs after ROLLBACK TO SAVEPOINT but before ROLLBACK AND CHAIN.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (18, 18)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 400 WHERE key = 12"));
  ASSERT_OK(conn.Execute("ROLLBACK AND CHAIN"));
  // Now in new transaction started by ROLLBACK AND CHAIN.
  ASSERT_OK(conn.Execute("INSERT INTO test_table values (16, 16)"));
  ASSERT_OK(conn.Execute("DELETE FROM test_table WHERE key = 15"));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // Both transactions were rolled back, so CDC should see no records.
  ASSERT_OK(WaitFor(
    [&]() -> Result<bool> {
      auto resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id));
      return resp.cdc_sdk_proto_records_size() == 0;
    },
    MonoDelta::FromSeconds(10), "Expected 0 records (both transactions rolled back)"));
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestHiddenTableDeletesAfterCompletelyPolled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table"));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(
      stream_id, {table.table_id()}, kVWALSessionId1, nullptr /*vslot_hash_range */,
      true /* include_oid_to_relfilenode */));

  vector<string> cmds = {
      // Adding a column with volatile default value
      "ALTER TABLE $0 ADD COLUMN value_2 INT DEFAULT random()",
      // Adding a column with auto incrementing integer
      "ALTER TABLE $0 ADD COLUMN value_3 SERIAL",
      // Altering the column data type to non binary-compatible
      "ALTER TABLE $0 ALTER COLUMN value_1 TYPE DOUBLE PRECISION",
      // Altering the column using USING clause
      "ALTER TABLE $0 ALTER COLUMN value_1 TYPE INTEGER USING (value_2 * 100)::INTEGER",
      // Truncating the table
      "TRUNCATE TABLE $0",
      // Dropping the table
      "DROP TABLE $0"};

  for (int i = 0; i < static_cast<int>(cmds.size()); i++) {
    auto old_table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table"));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> old_tablets;
    ASSERT_OK(test_client()->GetTablets(old_table, 0, &old_tablets, nullptr));
    ASSERT_EQ(old_tablets.size(), 1);

    ASSERT_OK(WriteRowsHelper(i, i + 1, &test_cluster_, true, 2, old_table.table_name().c_str()));

    ASSERT_OK(conn.ExecuteFormat(cmds[i], old_table.table_name()));

    YBTableName new_table;
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> new_tablets;
    if (cmds[i].find("DROP TABLE") == string::npos) {
      new_table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table"));
      ASSERT_OK(test_client()->GetTablets(new_table, 0, &new_tablets, nullptr));
      ASSERT_EQ(new_tablets.size(), 1);
    }

    ASSERT_OK(PollTillRestartTimeExceedsTableHideTime(stream_id, old_table, new_table));

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          SleepFor(MonoDelta::FromSeconds(1));
          return test_client()
              ->GetTabletsFromTableId(old_table.table_id(), 0, &old_tablets)
              .IsNotFound();
        },
        MonoDelta::FromSeconds(60),
        Format("Timed out waiting for hidden table $0 to be deleted", old_table.table_name())));

    auto expected_tablets_in_cdc_state_table =
        new_table.empty()
            ? std::unordered_set<TabletId>{kCDCSDKSlotEntryTabletId}
            : std::unordered_set<TabletId>{new_tablets[0].tablet_id(), kCDCSDKSlotEntryTabletId};
    CheckTabletsInCDCStateTable(
        expected_tablets_in_cdc_state_table, test_client(), stream_id,
        "Timed out waiting for state table entries to get deleted",
        true /* include_catalog_tables */);

    auto expected_tables_in_stream_metadata =
        new_table.empty() ? std::unordered_set<std::string>{}
                          : std::unordered_set<std::string>{new_table.table_id()};
    VerifyTablesInStreamMetadata(
        stream_id, expected_tables_in_stream_metadata,
        Format(
            "Timed out waiting for hidden table $0 to be removed from stream metadata",
            old_table.table_name()),
        std::nullopt, true /* include_catalog_tables */);
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestHiddenTableDeletesOnceExpired) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 5 * 1000;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table"));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}));

  vector<string> cmds = {
      // Adding a column with volatile default value
      "ALTER TABLE $0 ADD COLUMN value_2 INT DEFAULT random()",
      // Adding a column with auto incrementing integer
      "ALTER TABLE $0 ADD COLUMN value_3 SERIAL",
      // Altering the column data type to non binary-compatible
      "ALTER TABLE $0 ALTER COLUMN value_1 TYPE DOUBLE PRECISION",
      // Altering the column using USING clause
      "ALTER TABLE $0 ALTER COLUMN value_1 TYPE INTEGER USING (value_2 * 100)::INTEGER",
      // Truncating the table
      "TRUNCATE TABLE $0",
      // Dropping the table
      "DROP TABLE $0"};

  ASSERT_OK(WriteRowsHelper(0, 1, &test_cluster_, true, 2, table.table_name().c_str()));

  // Consume the record just written and acknowledge it. This will set the restart time equal to its
  // commit time.
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);  // BEGIN, INSERT, COMMIT
  ASSERT_EQ(change_resp.cdc_sdk_proto_records().Get(2).row_message().op(), RowMessage_Op_COMMIT);
  uint64_t commit_lsn = change_resp.cdc_sdk_proto_records().Get(2).row_message().pg_lsn();
  ASSERT_OK(UpdateAndPersistLSN(stream_id, commit_lsn, commit_lsn));

  // In each iteration of this loop, we perform a DDL which will cause its tablet to be hidden (and
  // conditionally create new tablet). We will no longer call GetConsistentChanges, meaning that the
  // restart time will always be behind the hide time of this tablet (DDL's commit time). At the
  // end of the iteration we assert that the hidden tablet has been deleted due to expiry (We dont
  // keep a tablet hidden for more than FLAGS_cdc_intent_retention_ms). We will also assert that the
  // new tablet, if created, is not deleted by this logic.
  for (int i = 0; i < static_cast<int>(cmds.size()); i++) {
    auto old_table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table"));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> old_tablets;
    ASSERT_OK(test_client()->GetTablets(old_table, 0, &old_tablets, nullptr));
    ASSERT_EQ(old_tablets.size(), 1);

    // Execute the DDL.
    ASSERT_OK(conn.ExecuteFormat(cmds[i], old_table.table_name()));

    YBTableName new_table;
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> new_tablets;
    if (cmds[i].find("DROP TABLE") == string::npos) {
      new_table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table"));
      ASSERT_OK(test_client()->GetTablets(new_table, 0, &new_tablets, nullptr));
      ASSERT_EQ(new_tablets.size(), 1);
    }

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          SleepFor(MonoDelta::FromSeconds(1));
          auto old_tablets_deleted =
              test_client()
                  ->GetTabletsFromTableId(old_table.table_id(), 0, &old_tablets)
                  .IsNotFound();

          auto new_tablets_deleted = false;
          if (cmds[i].find("DROP TABLE") == string::npos) {
            RETURN_NOT_OK(
                test_client()->GetTabletsFromTableId(new_table.table_id(), 0, &new_tablets));
            new_tablets_deleted = (new_tablets.size() == 0);
          }

          return old_tablets_deleted && !new_tablets_deleted;
        },
        MonoDelta::FromSeconds(60),
        Format("Timed out waiting for hidden table $0 to be deleted", old_table.table_name())));

    auto expected_tablets_in_cdc_state_table =
        new_table.empty()
            ? std::unordered_set<TabletId>{kCDCSDKSlotEntryTabletId}
            : std::unordered_set<TabletId>{new_tablets[0].tablet_id(), kCDCSDKSlotEntryTabletId};
    CheckTabletsInCDCStateTable(expected_tablets_in_cdc_state_table, test_client(), stream_id);

    auto expected_tables_in_stream_metadata =
        new_table.empty() ? std::unordered_set<std::string>{}
                          : std::unordered_set<std::string>{new_table.table_id()};
    VerifyTablesInStreamMetadata(
        stream_id, expected_tables_in_stream_metadata,
        Format(
            "Timed out waiting for hidden table $0 to be removed from stream metadata",
            old_table.table_name()));
  }
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestDropSchemaHidesAssociatedTables) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 5 * 1000;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto schema_name = "test_schema";
  ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", schema_name));

  int num_tables = 5;
  vector<YBTableName> tables;
  vector<TableId> table_ids;
  vector<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> tablets(num_tables);
  for (int i = 0; i < num_tables; i++) {
    auto table = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, Format("test_table_$0", i), 1, true, false, 0, false, "",
        schema_name));
    tables.push_back(table);
    table_ids.push_back(table.table_id());
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets[i], nullptr));
    ASSERT_EQ(tablets[i].size(), 1);
  }

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  for (const auto& table : tables) {
    ASSERT_OK(WriteRows(0, 1, &test_cluster_, 2, Format("$0.$1", schema_name, table.table_name())));
  }

  ASSERT_OK(InitVirtualWAL(stream_id, table_ids));

  ASSERT_OK(conn.ExecuteFormat("DROP SCHEMA $0 CASCADE", schema_name));

  for (int i = 0; i < num_tables; i++) {
    auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 3);  // BEGIN, INSERT, COMMIT
  }

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        SleepFor(MonoDelta::FromSeconds(1));
        bool result = true;
        for (int i = 0; i < num_tables; i++) {
          result = result && test_client()
                                 ->GetTabletsFromTableId(tables[i].table_id(), 0, &tablets[i])
                                 .IsNotFound();
        }
        return result;
      },
      MonoDelta::FromSeconds(60), "Timed out waiting for hidden tables to be deleted"));
  CheckTabletsInCDCStateTable({kCDCSDKSlotEntryTabletId}, test_client(), stream_id);
  VerifyTablesInStreamMetadata(
      stream_id, {}, "Timed out waiting for hidden tables to be removed from stream metadata");
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestUnackRecordsPolledFromHiddenTableOnVWALRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  auto table1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table1_tablets;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &table1_tablets, nullptr));
  ASSERT_EQ(table1_tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(
      stream_id, {table1.table_id()}, kVWALSessionId1, nullptr /*vslot_hash_range */,
      true /* include_oid_to_relfilenode */));

  ASSERT_OK(WriteRowsHelper(0, 1, &test_cluster_, true, 2, table1.table_name().c_str()));

  ASSERT_OK(conn.ExecuteFormat("TRUNCATE TABLE $0", table1.table_name()));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, table1.table_name()));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table2_tablets;
  ASSERT_OK(test_client()->GetTablets(table2, 0, &table2_tablets, nullptr));
  ASSERT_EQ(table2_tablets.size(), 1);

  auto slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
  auto restart_time = slot_entry->record_id_commit_time;

  // Do multiple GCC calls to ensure that all records from truncate are polled. Given that we are
  // not ack'ing the records, the restart time should not move ahead.
  for (int i = 0; i < 5; i++) {
    auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
    slot_entry = ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id));
    ASSERT_EQ(restart_time, slot_entry->record_id_commit_time);
  }
  // Sleep to allow parent tablet deletion task to run. The table shouldn't get deleted by
  // background task as we have not acked the records yet.
  SleepFor(MonoDelta::FromSeconds(FLAGS_cdc_parent_tablet_deletion_task_retry_secs * 4));
  ASSERT_OK(test_client()->GetTabletsFromTableId(table1.table_id(), 0, &table1_tablets));

  // Restart VWAL
  ASSERT_OK(DestroyVirtualWAL());
  ASSERT_OK(InitVirtualWAL(
      stream_id, {table1.table_id()}, kVWALSessionId1, nullptr /*vslot_hash_range */,
      true /* include_oid_to_relfilenode */));

  ASSERT_OK(WriteRowsHelper(0, 1, &test_cluster_, true, 2, table2.table_name().c_str()));

  // VWAL will switch to table2 on detecting the TRUNCATE.
  ASSERT_OK(PollTillRestartTimeExceedsTableHideTime(stream_id, table1, table2));

  // Now, the old table should get deleted as we have acked all records including those from
  // truncate.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        SleepFor(MonoDelta::FromSeconds(1));
        return test_client()
            ->GetTabletsFromTableId(table1.table_id(), 0, &table1_tablets)
            .IsNotFound();
      },
      MonoDelta::FromSeconds(60),
      Format("Timed out waiting for hidden table $0 to be deleted", table1.table_name())));
  CheckTabletsInCDCStateTable(
      {table2_tablets[0].tablet_id(), kCDCSDKSlotEntryTabletId}, test_client(), stream_id,
      "Timed out waiting for state table entries to get deleted",
      true /* include_catalog_tables */);
  VerifyTablesInStreamMetadata(
      stream_id, {table2.table_id()},
      "Timed out waiting for hidden table $0 to be removed from stream metadata",
      std::nullopt /* expected_unqualified_table_ids*/, true /* include_catalog_tables */);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestDDLOnTableWithoutPrimaryKey) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_cdcsdk_stream_tables_without_primary_key) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 0;
  // ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_enable_dynamic_table_support) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table_without_pk = ASSERT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, "test_table", 1 /* num_tablets */,
      false /* add_primary_key */));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table_without_pk, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(
      stream_id, {table_without_pk.table_id()}, kVWALSessionId1, nullptr /* slot_hash_range */,
      true /* include_oid_to_relfilenode */));

  ASSERT_OK(WriteRowsHelper(0, 1, &test_cluster_, true, 2, table_without_pk.table_name().c_str()));
  auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table_without_pk.table_id()}, 1 /* expected_dml_records */,
      false /* init_virtual_wal */, kVWALSessionId1));
  ASSERT_EQ(get_consistent_changes_resp.records.size(), 3);

  // Add the primary key contraint
  ASSERT_OK(
      conn.ExecuteFormat("ALTER TABLE $0 ADD PRIMARY KEY (key)", table_without_pk.table_name()));
  auto table_with_pk = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table"));
  ASSERT_OK(test_client()->GetTablets(table_with_pk, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  ASSERT_OK(PollTillRestartTimeExceedsTableHideTime(stream_id, table_without_pk, table_with_pk));

  ASSERT_OK(WriteRowsHelper(1, 2, &test_cluster_, true, 2, table_with_pk.table_name().c_str()));
  get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table_with_pk.table_id()}, 1 /* expected_dml_records */,
      false /* init_virtual_wal */, kVWALSessionId1));
  ASSERT_EQ(get_consistent_changes_resp.records.size(), 3);

  // Removing the primary key contraint
  ASSERT_OK(
      conn.ExecuteFormat("ALTER TABLE $0 DROP CONSTRAINT $0_pkey", table_with_pk.table_name()));
  auto table_with_dropped_pk =
      ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table"));
  ASSERT_OK(test_client()->GetTablets(table_with_dropped_pk, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  ASSERT_OK(
      PollTillRestartTimeExceedsTableHideTime(stream_id, table_with_pk, table_with_dropped_pk));

  ASSERT_OK(
      WriteRowsHelper(2, 3, &test_cluster_, true, 2, table_with_dropped_pk.table_name().c_str()));
  get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table_with_dropped_pk.table_id()}, 1 /* expected_dml_records */,
      false /* init_virtual_wal */, kVWALSessionId1));
  ASSERT_EQ(get_consistent_changes_resp.records.size(), 3);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestSplitParentTabletAndHiddenTableGetsPolledAndDeleted) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_update_restart_time_interval_secs) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* populate_safepoint_record */));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  auto old_table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> old_tablets;
  ASSERT_OK(test_client()->GetTablets(old_table, 0, &old_tablets, nullptr));
  ASSERT_EQ(old_tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  ASSERT_OK(WriteRows(0, 10, &test_cluster_, 2, old_table.table_name().c_str()));

  ASSERT_OK(WaitForFlushTables({old_table.table_id()}, false, 30, true));
  WaitUntilSplitIsSuccesful(old_tablets[0].tablet_id(), old_table, 2);
  // Get all the tablets including hidden tablets for old_table. We should get 3 tablets, i.e. 2
  // children and 1 un-deleted hidden parent tablet.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> old_tablets_after_split;
  ASSERT_OK(
      test_client()->GetTablets(
          old_table, 0, &old_tablets_after_split, nullptr, RequireTabletsRunning::kFalse,
          master::IncludeInactive::kTrue));
  ASSERT_EQ(old_tablets_after_split.size(), 3);

  ASSERT_OK(WriteRows(10, 15, &test_cluster_, 2, old_table.table_name().c_str()));

  ASSERT_OK(conn.ExecuteFormat("TRUNCATE TABLE $0", old_table.table_name()));
  auto new_table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, old_table.table_name()));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> new_tablets;
  ASSERT_OK(test_client()->GetTablets(new_table, 0, &new_tablets, nullptr));
  ASSERT_EQ(new_tablets.size(), 1);

  ASSERT_OK(WriteRows(0, 5, &test_cluster_, 2, new_table.table_name().c_str()));

  ASSERT_OK(InitVirtualWAL(
      stream_id, {old_table.table_id()}, kVWALSessionId1, nullptr /*vslot_hash_range */,
      true /* include_oid_to_relfilenode */));

  auto resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {old_table.table_id()}, 20, false /* init_vwal */, kVWALSessionId1,
      true /* allow_sending_feedback */, nullptr /* slot_hash_range */, {new_table.table_id()}));
  ASSERT_EQ(resp.records.size(), 60);

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        SleepFor(MonoDelta::FromSeconds(1));
        return test_client()
            ->GetTabletsFromTableId(old_table.table_id(), 0, &old_tablets)
            .IsNotFound();
      },
      MonoDelta::FromSeconds(60),
      Format("Timed out waiting for hidden table $0 to be deleted", old_table.table_name())));
  CheckTabletsInCDCStateTable(
      {new_tablets[0].tablet_id(), kCDCSDKSlotEntryTabletId}, test_client(), stream_id,
      "Timed out waiting for state table entries to get deleted",
      true /* include_catalog_tables */);
  VerifyTablesInStreamMetadata(
      stream_id, {new_table.table_id()},
      Format(
          "Timed out waiting for hidden table $0 to be removed from stream metadata",
          old_table.table_name()),
      std::nullopt /* expected_unqualified_table_ids*/, true /* include_catalog_tables */);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestOnlyReWriteCausingDDLsTriggerPubRefresh) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  google::SetVLOGLevel("cdcsdk_virtual_wal", 3);
  auto table =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1 /* num_tablets*/));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(
      stream_id, {table.table_id()}, kVWALSessionId1, nullptr,
      true /* include_oid_to_relfilenode */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Any DDL which causes table rewrite should send the pub refresh signal to the walsender.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN serial_col SERIAL"));

  // Consume the DDL record.
  auto change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 1);
  ASSERT_EQ(change_resp.cdc_sdk_proto_records()[0].row_message().op(), RowMessage_Op_DDL);

  // Next GetConsistentChanges will signal for pub refresh.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp.has_needs_publication_table_list_refresh() &&
      change_resp.needs_publication_table_list_refresh() &&
      change_resp.has_publication_refresh_time());
  ASSERT_GT(change_resp.publication_refresh_time(), 0);

  auto new_table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(UpdatePublicationTableList(
      stream_id, {new_table.table_id()}, kVWALSessionId1, true /* include_oid_to_relfilenode */));

  // Any DDL which does not lead to a table rewrite should not send a pub refresh signal to the
  // walsender.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN text_col text"));

  // No pub refresh, since no table rewrite. This empty response is due to sys catalog tablet queue
  // becoming empty.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records_size(), 0);
  ASSERT_FALSE(
      change_resp.has_needs_publication_table_list_refresh() ||
      change_resp.has_publication_refresh_time());

  // Consume the DDL record.
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id));
  ASSERT_EQ(change_resp.cdc_sdk_proto_records()[0].row_message().op(), RowMessage_Op_DDL);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    TestSlotWithImplicitPublicationChangesDetectionWhenFeatureDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Trying to use a slot which has detect_publication_changes_implicitly set to true while the flag
  // ysql_yb_enable_implicit_dynamic_tables_logical_replication is disabled should result in an
  // error.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      false;
  ASSERT_NOK(InitVirtualWAL(
      stream_id, {} /* table_ids */, kVWALSessionId1, nullptr,
      true /* include_oid_to_relfilenode */, 0 /* timeout*/));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;
  ASSERT_OK(InitVirtualWAL(
      stream_id, {} /* table_ids */, kVWALSessionId1, nullptr,
      true /* include_oid_to_relfilenode */));
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestPubRefreshStreamsWorkFineAfterUpgrade) {
  // Start with ysql_yb_enable_implicit_dynamic_tables_logical_replication set to false, simulating
  // pre-upgrade universe.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) = 5;
  // Set this to avoid destruction of VWALs due to them being classifed as expired sessions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_before_complete_expired_pg_sessions_shutdown_ms) =
      100 * 1000;

  google::SetVLOGLevel("cdcsdk_virtual_wal", 3);
  ASSERT_OK(SetUpWithParams(
      1 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  // Create a single tablet table.
  auto table =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1 /* num_tablets*/));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create a pub refresh slot.
  auto pub_refresh_slot = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Set the flag ysql_yb_enable_implicit_dynamic_tables_logical_replication to true. This simulates
  // an upgrade.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) =
      true;

  ASSERT_OK(InitVirtualWAL(
      pub_refresh_slot, {table.table_id()} /* table_ids */, 1 /* session_id */, nullptr,
      true /* include_oid_to_relfilenode */));

  auto change_resp_1 =
      ASSERT_RESULT(GetConsistentChangesFromCDC(pub_refresh_slot, 1 /* session_id */));
  ASSERT_TRUE(change_resp_1.cdc_sdk_proto_records().empty());

  // Sleep so that the next record to be streamed from pub_refresh_slot is a pub refresh record.
  SleepFor(MonoDelta::FromSeconds(
      FLAGS_cdcsdk_publication_list_refresh_interval_secs * 2 * kTimeMultiplier));
  change_resp_1 = ASSERT_RESULT(GetConsistentChangesFromCDC(pub_refresh_slot, 1 /* session_id */));
  ASSERT_EQ(change_resp_1.cdc_sdk_proto_records_size(), 0);
  ASSERT_TRUE(
      change_resp_1.has_needs_publication_table_list_refresh() &&
      change_resp_1.needs_publication_table_list_refresh() &&
      change_resp_1.has_publication_refresh_time());
  ASSERT_GT(change_resp_1.publication_refresh_time(), 0);
}

// This test verifies that GetConsistentChanges RPC call doesn't timeout even when there are
// many tablets. The VWAL's GetConsistentChangesInternal will only make internal GetChanges() calls
// to a max of FLAGS_cdcsdk_tablets_to_poll_batch_size tablets.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestGetConsistentChangesWithManyTablets) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_vwal_tablets_to_poll_batch_size) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_vwal_getchanges_rpc_delay_ms) = 1000;

  constexpr int kNumTables = 5;
  constexpr int kTabletsPerTable = 5;

  ASSERT_OK(SetUpWithParams(
      3 /* rf */, 1 /* num_masters */, false /* colocated */,
      true /* cdc_populate_safepoint_record */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  std::vector<client::YBTableName> tables;
  std::vector<TableId> table_ids;
  tables.reserve(kNumTables);
  table_ids.reserve(kNumTables);

  for (int i = 0; i < kNumTables; ++i) {
    std::string table_name = Format("test_table_$0", i);
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id int primary key, value_1 int) SPLIT INTO $1 TABLETS", table_name,
        kTabletsPerTable));

    auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, table_name));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
    ASSERT_EQ(tablets.size(), kTabletsPerTable);

    tables.push_back(table);
    table_ids.push_back(table.table_id());
  }

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  ASSERT_OK(InitVirtualWAL(stream_id, table_ids, kVWALSessionId1));

  // The batch size of tablets to poll in a single GetConsistentChanges call is
  // FLAGS_cdcsdk_vwal_tablets_to_poll_batch_size. So, we need to make ceil((kNumTables *
  // kTabletsPerTable) / FLAGS_cdcsdk_vwal_tablets_to_poll_batch_size) GetConsistentChanges calls to
  // consume all tablets.
  int num_get_changes_calls =
      ((kNumTables * kTabletsPerTable) + FLAGS_cdcsdk_vwal_tablets_to_poll_batch_size - 1) /
      FLAGS_cdcsdk_vwal_tablets_to_poll_batch_size;
  for (int i = 0; i < num_get_changes_calls; i++) {
    GetConsistentChangesRequestPB change_req;
    change_req.set_stream_id(stream_id.ToString());
    change_req.set_session_id(kVWALSessionId1);
    GetConsistentChangesResponsePB change_resp;
    RpcController get_changes_rpc;
    get_changes_rpc.set_timeout(
        MonoDelta::FromMilliseconds(
            2 * FLAGS_TEST_cdcsdk_vwal_getchanges_rpc_delay_ms *
            FLAGS_cdcsdk_vwal_tablets_to_poll_batch_size));
    auto status = cdc_proxy_->GetConsistentChanges(change_req, &change_resp, &get_changes_rpc);
    ASSERT_TRUE(status.ok() && !change_resp.has_error());
  }
}

}  // namespace cdc
}  // namespace yb
