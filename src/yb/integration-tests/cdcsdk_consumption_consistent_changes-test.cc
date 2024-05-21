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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
    google::SetVLOGLevel("cdcsdk_virtual_wal*", 3);
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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
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
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestExplicitCheckpointForMultiShardTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test1 (id int, value_1 int, PRIMARY KEY (ID ASC)) SPLIT AT VALUES ((2500))"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
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

  auto stream_id_1 = ASSERT_RESULT(CreateConsistentSnapshotStream(snapshot_option));

  std::unordered_map<TabletId, CdcStateTableRow> initial_tablet_checkpoint_table_1;
  for (const auto& tablet : table_1_tablets) {
    auto result = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_1, tablet.tablet_id()));
    initial_tablet_checkpoint_table_1[tablet.tablet_id()] = result;
    expected_table_one_tablets_with_progress.insert(tablet.tablet_id());
  }

  auto stream_id_2 = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 5_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 160;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 2 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1 (id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 250;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  int num_batches = 50;
  int inserts_per_batch = 100;

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

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 10;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
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

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);

  // Flushed transactions are replayed only if there is a cdc stream.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  // Sleep to ensure that alter table is committed in docdb
  // TODO: (#21288) Remove the sleep once the best effort waiting mechanism for drop table lands.
  SleepFor(MonoDelta::FromSeconds(5));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  // Sleep to ensure that alter table is committed in docdb
  // TODO: (#21288) Remove the sleep once the best effort waiting mechanism for drop table lands.
  SleepFor(MonoDelta::FromSeconds(5));

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
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

  // Create CDC stream.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  ASSERT_OK(WriteRowsHelper(1 /* start */, 11 /* end */, &test_cluster_, true));
  int expected_dml_records = 10;
  auto all_pending_consistent_change_resp = ASSERT_RESULT(
      GetAllPendingTxnsFromVirtualWAL(stream_id, {table.table_id()}, expected_dml_records, true));
  // Validate the columns and insert counts.
  ValidateColumnCounts(all_pending_consistent_change_resp, 2);
  CheckRecordCount(all_pending_consistent_change_resp, expected_dml_records);

  for (int nonkey_column_count = 2; nonkey_column_count < 15; ++nonkey_column_count) {
    std::string added_column_name = "value_" + std::to_string(nonkey_column_count);
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, added_column_name));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 25;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 25;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 25;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 15;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 20;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
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
  xrepl::StreamId stream_1 = ASSERT_RESULT(CreateConsistentSnapshotStream());
  xrepl::StreamId stream_2 = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_publication_list_refresh_interval_secs) =
      publication_refresh_interval.ToSeconds();

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  auto table_1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1"));

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // These arrays store counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, and COMMIT in
  // that order.
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int expected_count[] = {1, 3, 0, 0, 0, 0, 2, 2};

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
    // This is because in TSAN builds, the records of txn 2 are received by the VWAL in two
    // GetChanges calls instead of one. As a result VWAL does not ship a DDL record for txn 2 in
    // TSAN builds.
    if (IsTsan() && i == 0) {
      ASSERT_EQ(0, count[i]);
      continue;
    }
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
        "PopulateCDCStateTableOnNewTableCreation::Start"}});
  SyncPoint::GetInstance()->EnableProcessing();

  auto tablets = ASSERT_RESULT(SetUpWithOneTablet(1, 1, false));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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

  ASSERT_RESULT(CreateConsistentSnapshotStream());
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

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestConsumptionAfterDroppingTableNotInPublication) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 20;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());
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
    CDCStateTable cdc_state_table(test_client());
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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  if (multiple_streams) {
    ASSERT_RESULT(CreateConsistentSnapshotStream());
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
      MonoDelta::FromSeconds(10), "Timed out waiting for slot entry deletion from state table"));

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
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

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

}  // namespace cdc
}  // namespace yb
