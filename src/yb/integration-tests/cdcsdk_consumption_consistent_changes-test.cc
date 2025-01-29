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
        auto txn_participant = ASSERT_RESULT(peer->shared_tablet_safe())->transaction_participant();
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
    TestParallelConsumptionFromMultipleVWALWithUseSnapshot) {
  TestConcurrentConsumptionFromMultipleVWAL(CDCSDKSnapshotOption::USE_SNAPSHOT);
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
    ASSERT_EQ(record.row_message().table(), "test1");
    UpdateRecordCount(record, count);
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
    ASSERT_EQ(record.row_message().table(), "test1");
    UpdateRecordCount(record, count);
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
  auto delta = commit_time.PhysicalDiff(cdcsdk_consistent_snapshot_time);

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

// Test for the possible race condition between create tablet and UpdatePeersAndMetrics thread. In
// this test we verify that UpdatePeersAndMetrics does not remove the retention barrier on the
// dynamically created tablets before their entries are added to the cdc_state table.
TEST_F(CDCSDKConsumptionConsistentChangesTest, TestRetentionBarrierRaceWithUpdatePeersAndMetrics) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 10;
  google::SetVLOGLevel("tablet*", 1);
  SyncPoint::GetInstance()->LoadDependency(
      {{"SetupCDCSDKRetentionOnNewTablet::End", "UpdatePeersAndMetrics::Start"},
       {"UpdateTabletPeersWithMaxCheckpoint::Done",
        "PopulateCDCStateTableOnNewTableCreation::Start"},
       {"ProcessNewTablesForCDCSDKStreams::Start",
        "PopulateCDCStateTableOnNewTableCreation::End"}});
  SyncPoint::GetInstance()->EnableProcessing();

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());

  // Create another table after stream creation.
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table2"));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  auto tablet_peer =
      ASSERT_RESULT(GetLeaderPeerForTablet(test_cluster(), tablets.begin()->tablet_id()));

  // Verify that table has been added to the stream.
  ASSERT_OK(WaitFor(
                [&]() -> Result<bool> {
                  auto stream_info = VERIFY_RESULT(GetDBStreamInfo(stream_id));
                  for (auto table_info : stream_info.table_info()) {
                    if (table_info.table_id() == table.table_id()) {
                      return true;
                    }
                  }
                  return false;
                },
                MonoDelta::FromSeconds(60),
                "Timed out waiting for the table to get added to stream"));

  // Check that UpdatePeersAndMetrics has not removed retention barriers.
  auto checkpoint_result =
      ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablet_peer->tablet_id()));
  LogRetentionBarrierAndRelatedDetails(checkpoint_result, tablet_peer);
  // A dynamically added table in replication slot consumption will not be snapshotted and will have
  // replica identity "CHANGE". Hence the cdc_sdk_safe_time in tablet peer will be invalid.
  ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
  ASSERT_LT(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  ASSERT_LT(tablet_peer->cdc_sdk_min_checkpoint_op_id(), OpId::Max());

  // Now, drop the consistent snapshot stream and check that retention barriers are released.
  ASSERT_TRUE(DeleteCDCStream(stream_id));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 1;
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

  if (multiple_streams) {
    // Since one stream still exists, the retention barriers will not be lifted.
    ASSERT_NE(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
    ASSERT_NE(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  } else {
    // Since the only stream that existed is now deleted, the retention barriers will be unset.
    VerifyTransactionParticipant(tablet_peer->tablet_id(), OpId::Max());
    ASSERT_EQ(tablet_peer->get_cdc_sdk_safe_time(), HybridTime::kInvalid);
    ASSERT_EQ(tablet_peer->get_cdc_min_replicated_index(), OpId::Max().index);
  }
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
      "test_slot", CDCSDKSnapshotOption::USE_SNAPSHOT, false, kNamespaceName_2));

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
      "test_slot", CDCSDKSnapshotOption::USE_SNAPSHOT, false /*verify_snapshot_name*/,
      kNamespaceName));
  // Since a replication slot is created on kNamespace, creation of old model stream should fail.
  ASSERT_NOK(CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::CHANGE, kNamespaceName));

  ASSERT_RESULT(
      CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::CHANGE, kNamespaceName_2));
  // Since a old model stream is created on kNamespace_2, creation of replication slot should fail.
  ASSERT_NOK(CreateConsistentSnapshotStreamWithReplicationSlot(
      "test_slot_2", CDCSDKSnapshotOption::USE_SNAPSHOT, false /*verify_snapshot_name*/,
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

}  // namespace cdc
}  // namespace yb
