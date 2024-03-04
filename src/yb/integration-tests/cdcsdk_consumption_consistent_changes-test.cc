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
  void TestCDCSDKConsistentStreamWithTabletSplit(CDCCheckpointType checkpoint_type);
};

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestVirtualWAL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestConcurrentConsumptionFromMultipleVirtualWAL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table1 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test1", 3));
  auto table2 = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test2", 3));

  // insert 50 records in both tables. This will not received in streaming phase.
  ASSERT_OK(WriteRowsHelper(1, 50, &test_cluster_, true, 2, "test1"));
  ASSERT_OK(WriteRowsHelper(1, 50, &test_cluster_, true, 2, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table_1_tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table_2_tablets;
  ASSERT_OK(test_client()->GetTablets(table1, 0, &table_1_tablets, nullptr));
  ASSERT_EQ(table_1_tablets.size(), 3);
  ASSERT_OK(test_client()->GetTablets(table1, 0, &table_2_tablets, nullptr));
  ASSERT_EQ(table_2_tablets.size(), 3);
  auto stream_id_1 = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

  auto stream_id_2 = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

  int num_batches = 50;
  // 50 inserts per batch.
  for (int i = 1; i <= num_batches; i++) {
    ASSERT_OK(WriteRowsHelper(
        i * 50 /* start */, (i * 50 + 50) /* end */, &test_cluster_, true, 2, "test1"));
    ASSERT_OK(WriteRowsHelper(
        i * 50 /* start */, (i * 50 + 50) /* end */, &test_cluster_, true, 2, "test2"));
  }

  for (int i = 51; i <= (num_batches + 50); i++) {
    ASSERT_OK(WriteRowsHelper(
        i * 50 /* start */, (i * 50 + 50) /* end */, &test_cluster_, true, 2, "test2"));
  }

  int expected_dml_records_table_1 = 2500;  // starting from [50, 100) to [2500, 2550)
  int expected_dml_records_table_2 = 5000;  // starting from [50, 100) to [5000, 5050)
  std::thread t1([&]() -> void {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingChangesFromCdc(
        stream_id_1, {table1.table_id()}, expected_dml_records_table_1, true, 1));
    LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 1.";

    CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
    CheckRecordCount(get_consistent_changes_resp, expected_dml_records_table_1);
  });
  std::thread t2([&]() -> void {
    auto get_consistent_changes_resp = ASSERT_RESULT(GetAllPendingChangesFromCdc(
        stream_id_2, {table2.table_id()}, expected_dml_records_table_2, true, 2));
    LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records from table 2.";

    CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
    CheckRecordCount(get_consistent_changes_resp, expected_dml_records_table_2);
  });

  t1.join();
  t2.join();
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithGenerateSeries) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 250;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test_table ($0, $1) select i, i+1 from generate_series(1,1000) as i",
      kKeyColumnName, kValueColumnName));

  int expected_dml_records = 1000;
  auto get_consistent_changes_resp = ASSERT_RESULT(
      GetAllPendingChangesFromCdc(stream_id, {table.table_id()}, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithManyTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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
  auto get_consistent_changes_resp = ASSERT_RESULT(
      GetAllPendingChangesFromCdc(stream_id, {table.table_id()}, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithForeignKeys) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 50;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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
  auto get_consistent_changes_resp = ASSERT_RESULT(
      GetAllPendingChangesFromCdc(stream_id, {table2.table_id()}, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithAbortedTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_consistent_records) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 10;
  ASSERT_OK(SetUpWithParams(3, 1, false, true));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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
  auto get_consistent_changes_resp = ASSERT_RESULT(
      GetAllPendingChangesFromCdc(stream_id, {table.table_id()}, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithColocation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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
  auto get_consistent_changes_resp =
      ASSERT_RESULT(GetAllPendingChangesFromCdc(stream_id, table_ids, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

void CDCSDKConsumptionConsistentChangesTest::TestCDCSDKConsistentStreamWithTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
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
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, true));
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
  auto get_consistent_changes_resp = ASSERT_RESULT(
      GetAllPendingChangesFromCdc(stream_id, {table.table_id()}, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

TEST_F(CDCSDKConsumptionConsistentChangesTest, TestCDCSDKConsistentStreamWithTabletSplitImplicit) {
  TestCDCSDKConsistentStreamWithTabletSplit(CDCCheckpointType::IMPLICIT);
}

TEST_F(
    CDCSDKConsumptionConsistentChangesTest,
    YB_DISABLE_TEST_IN_TSAN(TestCDCSDKMakesProgressWithLongRunningTxn)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_resolve_intent_lag_threshold_ms) = 10 * 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 3));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);

  // Flushed transactions are replayed only if there is a cdc stream.
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_max_consistent_records) = 100;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));
  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream(
      CDCSDKSnapshotOption::USE_SNAPSHOT, CDCCheckpointType::IMPLICIT));

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
  auto get_consistent_changes_resp = ASSERT_RESULT(
      GetAllPendingChangesFromCdc(stream_id, {table.table_id()}, expected_dml_records, true));
  LOG(INFO) << "Got " << get_consistent_changes_resp.records.size() << " records.";

  CheckRecordsConsistencyWithWriteId(get_consistent_changes_resp.records);
  CheckRecordCount(get_consistent_changes_resp, expected_dml_records);
}

}  // namespace cdc
}  // namespace yb
