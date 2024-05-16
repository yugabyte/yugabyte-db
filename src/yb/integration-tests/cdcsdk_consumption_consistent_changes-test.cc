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
#include "yb/integration-tests/cdcsdk_ysql_test_base.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

class CDCSDKConsumptionConsistentChangesTest : public CDCSDKYsqlTest {
 public:
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
};

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVirtualWAL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, 1));

  // Sending the same session_id should result in error because a corresponding VirtualWAL instance
  // will already exist.
  ASSERT_NOK(InitVirtualWAL(stream_id, {table.table_id()}, 1));

  // Empty table list should succeed.
  ASSERT_OK(InitVirtualWAL(stream_id, {}, 2));
  ASSERT_OK(InitVirtualWAL(stream_id, {table.table_id()}, 3));

  ASSERT_OK(DestroyVirtualWAL(2));
  ASSERT_OK(DestroyVirtualWAL(3));

  // Sending the same session_id should result in error because the corresponding VirtualWAL
  // instance would have already been deleted.
  ASSERT_NOK(DestroyVirtualWAL(3));

  ASSERT_OK(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1));

  // Sending a different session_id for which VirtualWAL does not exist should result in error.
  ASSERT_NOK(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 3));
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestExplicitCheckpointForSingleShardTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestExplicitCheckpointForMultiShardTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

void CDCSDKConsumptionConsistentChangesTest::TestConcurrentConsumptionFromMultipleVWAL(
    CDCSDKSnapshotOption snapshot_option) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
        1 /* session_id */));
    LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 1.";

    auto last_record =
        get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
    VerifyLastRecordAndProgressOnSlot(stream_id_1, last_record);
    VerifyExplicitCheckpointingOnTablets(
        stream_id_1, initial_tablet_checkpoint_table_1, table_1_tablets,
        expected_table_one_tablets_with_progress);
    CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
    CheckRecordCount(get_consistent_changes_resp, expected_dml_records_table_1);
  });

  std::thread t2([&]() -> void {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
        stream_id_2, {table2.table_id()}, expected_dml_records_table_2, true /* init_virtual_wal */,
        2 /* session_id */));
    LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 2.";

    auto last_record =
        get_consistent_changes_resp.records[get_consistent_changes_resp.records.size() - 1];
    VerifyLastRecordAndProgressOnSlot(stream_id_2, last_record);
    VerifyExplicitCheckpointingOnTablets(
        stream_id_2, initial_tablet_checkpoint_table_2, table_2_tablets,
        expected_table_two_tablets_with_progress);
    CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
      1 /* session_id */, false /* allow_sending_feedback */));
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
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, 1 /* session_id */));

  // Call GetConsistentChanges twice so as to send explicit checkpoint on both the tablets. Two
  // calls because in one call, only 1 tablet queue would be empty and the other tablet queue would
  // have a safepoint record. So, we'll call GetChanges only the empty tablet queue.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1 /* session_id */));
  // Since we had already consumed all records, we dont expect any records in these
  // GetConsistentChanges calls.
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1 /* session_id */));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(DestroyVirtualWAL(1 /* session_id */));

  expected_dml_records = inserts_per_batch * 2;  // 4 & 5th txn expected
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      2 /* session_id */, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";
  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 2 /* session_id */));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
      1 /* session_id */, false /* allow_sending_feedback */));
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
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, 1 /* session_id */));

  // Call GetConsistentChanges twice so as to send explicit checkpoint on both the tablets. Two
  // calls because in one call, only 1 tablet queue would be empty and the other tablet queue would
  // have a safepoint record. So, we'll call GetChanges only the empty tablet queue.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1 /* session_id */));
  // Since we had already consumed all records, we dont expect any records in these
  // GetConsistentChanges calls.
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1 /* session_id */));
  ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(DestroyVirtualWAL(1));

  expected_dml_records = inserts_per_batch * 3;  // last 3 txns expected
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      2 /* session_id */, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";
  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 2 /* session_id */));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
      1 /* session_id */, false /* allow_sending_feedback */));
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
  ASSERT_GT(confirmed_flush_lsn, 0);
  ASSERT_GT(restart_lsn, 0);

  if (feedback_type == FeedbackType::commit_lsn_plus_one) {
    LOG(INFO) << "Incrementing commit_lsn " << restart_lsn << " by 1";
    restart_lsn += 1;
  }
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, 1 /* session_id */));

  // Out of the 2 tablet queues, one of them would contain the single-shard txn (150,151) and the
  // other one would be empty. So, in the next GetConsistentChanges, we will use from_checkpoint as
  // the explicit checkpoint while making GetChanges calls on the empty tablet queue.
  expected_dml_records = 1;
  resp = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, false /* init_virtual_wal */,
      1 /* session_id */, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp.records.size() << " records.";
  ASSERT_EQ(resp.records.size(), 3);

  // This will hold records of 6th txn.
  vector<CDCSDKProtoRecordPB> expected_records = resp.records;

  ASSERT_OK(DestroyVirtualWAL(1 /* session_id */));

  expected_dml_records = 1;
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      2 /* session_id */, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";

  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 2 /* session_id */));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
    auto resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}));
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
  ASSERT_OK(UpdateAndPersistLSN(stream_id, confirmed_flush_lsn, restart_lsn, 1 /* session_id */));

  // Consume the 4th txn completely i.e 1 BEGIN + 1000 DML + 1 COMMIT.
  while (expected_records.size() != 1002) {
    auto get_consistent_changes_resp = ASSERT_RESULT(
        GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1 /* session_id */));
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
  auto get_consistent_changes_resp = ASSERT_RESULT(
        GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 1 /* session_id */));
    LOG(INFO) << "Got " << get_consistent_changes_resp.cdc_sdk_proto_records_size() << " records.";
    ASSERT_EQ(get_consistent_changes_resp.cdc_sdk_proto_records_size(), 0);

  ASSERT_OK(DestroyVirtualWAL(1));

  int expected_dml_records = inserts_per_batch * 1;
  auto resp_after_restart = ASSERT_RESULT(GetAllPendingTxnsFromVirtualWAL(
      stream_id, {table.table_id()}, expected_dml_records, true /* init_virtual_wal */,
      2 /* session_id */, false /* allow_sending_feedback */));
  LOG(INFO) << "Got " << resp_after_restart.records.size() << " records.";

  // Verify that we receive no more records on GetConsistentChanges calls since we have consumed
  // everything.
  get_consistent_changes_resp =
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}, 2 /* session_id */));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithManyTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithForeignKeys) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 50;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithAbortedTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_consistent_records) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithColocation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVWALConsumptionOnMixTables) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithTabletSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 100_KB;

  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  int num_batches = 25;
  int inserts_per_batch = 100;

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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKMakesProgressWithLongRunningTxn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_resolve_intent_lag_threshold_ms) =
      10 * 1000 * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false));
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
      ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}));
  update_insert_count(change_resp);
  ASSERT_EQ(seen_insert_records, 0);
  change_resp = ASSERT_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}));
  update_insert_count(change_resp);
  ASSERT_EQ(seen_insert_records, 0);

  vector<CDCSDKProtoRecordPB> records;
  // Eventually, after FLAGS_cdc_resolve_intent_lag_threshold_ms time we should see the records for
  // the committed transaction.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp = VERIFY_RESULT(GetConsistentChangesFromCDC(stream_id, {table.table_id()}));
        update_insert_count(change_resp);
        for (const auto& record : change_resp.cdc_sdk_proto_records()) {
          records.push_back(record);
        }
        if (seen_insert_records == 500) return true;

        return false;
      },
      MonoDelta::FromSeconds(60), "Did not see all expected records"));

  CheckRecordsConsistencyWithWriteId(records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestConsistentSnapshotWithCDCSDKConsistentStream) {
  google::SetVLOGLevel("cdc*", 0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_TEST_enable_replication_slot_consumption) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
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

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

}  // namespace cdc
}  // namespace yb
