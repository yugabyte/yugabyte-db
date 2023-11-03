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
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithManyTransactions)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 40;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  int num_batches = 75;
  int inserts_per_batch = 100;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      1,
      2 * num_batches * inserts_per_batch,
      0,
      0,
      0,
      0,
      2 * num_batches + num_batches * inserts_per_batch,
      2 * num_batches + num_batches * inserts_per_batch,
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto get_changes_resp = GetAllPendingChangesFromCdc(stream_id, tablets);
  for (auto record : get_changes_resp.records) {
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(get_changes_resp.records);
  LOG(INFO) << "Got " << get_changes_resp.records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(30301, get_changes_resp.records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithForeignKeys)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 30;
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test1(id int primary key, value_1 int) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE test2(id int primary key, value_2 int, test1_id int, CONSTRAINT "
                   "fkey FOREIGN KEY(test1_id) REFERENCES test1(id)) SPLIT INTO 1 TABLETS"));

  auto table1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test1"));
  auto table2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table2, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(conn.Execute("INSERT INTO test1 VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("INSERT INTO test1 VALUES (2, 2)"));

  int queries_per_batch = 60;
  int num_batches = 60;
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

  ASSERT_OK(test_client()->FlushTables({table1.table_id()}, false, 1000, false));
  ASSERT_OK(test_client()->FlushTables({table2.table_id()}, false, 1000, false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      1, queries_per_batch * num_batches * 2,       queries_per_batch * num_batches * 2,       0, 0,
      0, num_batches * (4 + 2 * queries_per_batch), num_batches * (4 + 2 * queries_per_batch),
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto get_changes_resp = GetAllPendingChangesFromCdc(stream_id, tablets);
  for (auto record : get_changes_resp.records) {
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(get_changes_resp.records);

  LOG(INFO) << "Got " << get_changes_resp.records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(29281, get_changes_resp.records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithAbortedTransactions)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 30;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_consistent_records) = false;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

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

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      1, 29, 0, 0, 0, 0, 3, 3,
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto get_changes_resp = GetAllPendingChangesFromCdc(stream_id, tablets);
  for (auto record : get_changes_resp.records) {
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(get_changes_resp.records);

  LOG(INFO) << "Got " << get_changes_resp.records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(36, get_changes_resp.records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithTserverRestart)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  int num_batches = 75;
  int inserts_per_batch = 100;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 0; i < 150; i++) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test_table VALUES ($0, 1)", (2 * num_batches * inserts_per_batch) + i));
  }
  ASSERT_OK(conn.Execute("COMMIT"));

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 20, (2 * num_batches * inserts_per_batch) + 150);
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, (3 * num_batches * inserts_per_batch) + 150);
  });

  t3.join();
  t4.join();

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      2,
      4 * num_batches * inserts_per_batch + 150,
      0,
      0,
      0,
      0,
      4 * num_batches + 1 + 2 * num_batches * inserts_per_batch,
      4 * num_batches + 1 + 2 * num_batches * inserts_per_batch,
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto get_changes_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  vector<CDCSDKProtoRecordPB> all_records;
  for (int32_t i = 0; i < get_changes_resp.cdc_sdk_proto_records_size(); i++) {
    auto record = get_changes_resp.cdc_sdk_proto_records(i);
    all_records.push_back(record);
    UpdateRecordCount(record, count);
  }

  // Restart all tservers.
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    test_cluster()->mini_tablet_server(i)->Shutdown();
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->Start());
  }
  SleepFor(MonoDelta::FromSeconds(60));

  auto all_pending_changes = GetAllPendingChangesFromCdc(
      stream_id,
      tablets,
      &get_changes_resp.cdc_sdk_checkpoint(),
      0,
      get_changes_resp.safe_hybrid_time());
  for (size_t i = 0; i < all_pending_changes.records.size(); i++) {
    auto record = all_pending_changes.records[i];
    all_records.push_back(record);
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(all_records);
  LOG(INFO) << "Got " << all_records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(60754, all_records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithDDLStatements)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  int num_batches = 75;
  int inserts_per_batch = 100;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD value_2 int;"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP value_1;"));

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches,
        inserts_per_batch,
        "INSERT INTO test_table VALUES ($0, 1)",
        20,
        (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches,
        inserts_per_batch,
        "INSERT INTO test_table VALUES ($0, 1)",
        50,
        (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      3,
      4 * num_batches * inserts_per_batch,
      0,
      0,
      0,
      0,
      4 * num_batches + 2 * num_batches * inserts_per_batch,
      4 * num_batches + 2 * num_batches * inserts_per_batch,
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto get_changes_resp = GetAllPendingChangesFromCdc(stream_id, tablets);
  for (size_t i = 0; i < get_changes_resp.records.size(); i++) {
    auto record = get_changes_resp.records[i];
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(get_changes_resp.records);
  LOG(INFO) << "Got " << get_changes_resp.records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(60603, get_changes_resp.records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithLeadershipChange)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  int num_batches = 75;
  int inserts_per_batch = 100;

  std::thread t1(
      [&]() -> void { PerformSingleAndMultiShardInserts(num_batches, inserts_per_batch, 20); });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardInserts(
        num_batches, inserts_per_batch, 50, num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD value_2 int;"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP value_1;"));

  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches,
        inserts_per_batch,
        "INSERT INTO test_table VALUES ($0, 1)",
        20,
        (2 * num_batches * inserts_per_batch));
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches,
        inserts_per_batch,
        "INSERT INTO test_table VALUES ($0, 1)",
        50,
        (3 * num_batches * inserts_per_batch));
  });

  t3.join();
  t4.join();

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      4,
      4 * num_batches * inserts_per_batch,
      0,
      0,
      0,
      0,
      4 * num_batches + 2 * num_batches * inserts_per_batch,
      4 * num_batches + 2 * num_batches * inserts_per_batch,
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  size_t first_leader_index = 0;
  size_t first_follower_index = 0;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);

  auto get_changes_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  vector<CDCSDKProtoRecordPB> all_records;
  for (int32_t i = 0; i < get_changes_resp.cdc_sdk_proto_records_size(); i++) {
    auto record = get_changes_resp.cdc_sdk_proto_records(i);
    all_records.push_back(record);
    UpdateRecordCount(record, count);
  }

  // Leadership Change.
  ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));

  auto all_pending_changes = GetAllPendingChangesFromCdc(
      stream_id,
      tablets,
      &get_changes_resp.cdc_sdk_checkpoint(),
      0,
      get_changes_resp.safe_hybrid_time());
  for (size_t i = 0; i < all_pending_changes.records.size(); i++) {
    auto record = all_pending_changes.records[i];
    all_records.push_back(record);
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(all_records);
  LOG(INFO) << "Got " << all_records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(60604, all_records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithColocation)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));

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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  int num_batches = 50;
  int inserts_per_batch = 50;

  std::thread t1([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test1 VALUES ($0, 1)", 20);
  });
  std::thread t2([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches,
        inserts_per_batch,
        "INSERT INTO test1 VALUES ($0, 1)",
        50,
        num_batches * inserts_per_batch);
  });
  std::thread t3([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches, inserts_per_batch, "INSERT INTO test2 VALUES ($0, 1)", 20);
  });
  std::thread t4([&]() -> void {
    PerformSingleAndMultiShardQueries(
        num_batches,
        inserts_per_batch,
        "INSERT INTO test2 VALUES ($0, 1)",
        50,
        num_batches * inserts_per_batch);
  });

  t1.join();
  t2.join();
  t3.join();
  t4.join();

  ASSERT_OK(test_client()->FlushTables({table1.table_id()}, false, 1000, false));
  ASSERT_OK(test_client()->FlushTables({table2.table_id()}, false, 1000, false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count[] = {
      2,
      4 * num_batches * inserts_per_batch,
      0,
      0,
      0,
      0,
      8 * num_batches + 4 * num_batches * inserts_per_batch,
      8 * num_batches + 4 * num_batches * inserts_per_batch,
  };
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto get_changes_resp = GetAllPendingChangesFromCdc(stream_id, tablets);
  for (size_t i = 0; i < get_changes_resp.records.size(); i++) {
    auto record = get_changes_resp.records[i];
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(get_changes_resp.records);
  LOG(INFO) << "Got " << get_changes_resp.records.size() << " records.";
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count[i], count[i]);
  }
  ASSERT_EQ(30802, get_changes_resp.records.size());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  int num_batches = 75;
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
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

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

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in
  // that order.
  const int expected_count_1[] = {
      1,
      2 * num_batches * inserts_per_batch,
      0,
      0,
      0,
      0,
      2 * num_batches + num_batches * inserts_per_batch,
      2 * num_batches + num_batches * inserts_per_batch,
  };
  const int expected_count_2[] = {3, 4 * num_batches * inserts_per_batch, 0, 0, 0, 0};
  int count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  auto parent_get_changes = GetAllPendingChangesFromCdc(stream_id, tablets);
  for (size_t i = 0; i < parent_get_changes.records.size(); i++) {
    auto record = parent_get_changes.records[i];
    UpdateRecordCount(record, count);
  }

  CheckRecordsConsistency(parent_get_changes.records);
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_count_1[i], count[i]);
  }

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(2));

  auto get_tablets_resp =
      ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id, tablets[0].tablet_id()));
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    auto new_tablet = tablet_checkpoint_pair.tablet_locations();
    auto new_checkpoint = tablet_checkpoint_pair.cdc_sdk_checkpoint();

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    auto tablet_ptr = tablets.Add();
    tablet_ptr->CopyFrom(new_tablet);

    auto child_get_changes = GetAllPendingChangesFromCdc(stream_id, tablets, &new_checkpoint);
    vector<CDCSDKProtoRecordPB> child_plus_parent = parent_get_changes.records;
    for (size_t i = 0; i < child_get_changes.records.size(); i++) {
      auto record = child_get_changes.records[i];
      child_plus_parent.push_back(record);
      UpdateRecordCount(record, count);
    }
    CheckRecordsConsistency(child_plus_parent);
  }

  for (int i = 0; i < 6; i++) {
    ASSERT_EQ(expected_count_2[i], count[i]);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKHistoricalMaxOpId)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Should be -1.-1 in the beginning.
  ASSERT_EQ(GetHistoricalMaxOpId(tablets), OpId::Invalid());

  // Aborted transactions shouldn't change max_op_id.
  ASSERT_OK(WriteRowsHelper(0, 100, &test_cluster_, false));
  SleepFor(MonoDelta::FromSeconds(5));
  ASSERT_EQ(GetHistoricalMaxOpId(tablets), OpId::Invalid());

  // Committed transactions should change max_op_id.
  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  OpId historical_max_op_id;
  ASSERT_OK(WaitFor(
      [&]() {
        historical_max_op_id = GetHistoricalMaxOpId(tablets);
        return historical_max_op_id > OpId::Invalid();
      },
      MonoDelta::FromSeconds(5),
      "historical_max_op_id should change"));

  // Aborted transactions shouldn't change max_op_id.
  ASSERT_OK(WriteRowsHelper(200, 300, &test_cluster_, false));
  SleepFor(MonoDelta::FromSeconds(5));
  OpId new_historical_max_op_id = GetHistoricalMaxOpId(tablets);
  ASSERT_EQ(new_historical_max_op_id, historical_max_op_id);

  // Committed transactions should change max_op_id.
  ASSERT_OK(WriteRowsHelper(300, 400, &test_cluster_, true));
  ASSERT_OK(WaitFor(
      [&]() {
        new_historical_max_op_id = GetHistoricalMaxOpId(tablets);
        return new_historical_max_op_id > historical_max_op_id;
      },
      MonoDelta::FromSeconds(5),
      "historical_max_op_id should change"));
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKHistoricalMaxOpIdWithTserverRestart)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Should be -1.-1 in the beginning.
  ASSERT_EQ(GetHistoricalMaxOpId(tablets), OpId::Invalid());

  // Committed transactions should change max_op_id.
  ASSERT_OK(WriteRowsHelper(0, 100, &test_cluster_, true));
  OpId historical_max_op_id;
  ASSERT_OK(WaitFor(
      [&]() {
        historical_max_op_id = GetHistoricalMaxOpId(tablets);
        return historical_max_op_id > OpId::Invalid();
      },
      MonoDelta::FromSeconds(5),
      "historical_max_op_id should change"));

  // Restart all tservers.
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    test_cluster()->mini_tablet_server(i)->Shutdown();
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->Start());
  }

  // Should be same as before restart.
  ASSERT_OK(WaitFor(
      [&]() { return GetHistoricalMaxOpId(tablets) == historical_max_op_id; },
      MonoDelta::FromSeconds(30),
      "historical_max_op_id should be same as before restart"));
}

TEST_F(
    CDCSDKYsqlTest,
    YB_DISABLE_TEST_IN_TSAN(TestCDCSDKHistoricalMaxOpIdTserverRestartWithFlushTables)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Flushed transactions are replayed only if there is a cdc stream.
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // Should be -1.-1 in the beginning.
  ASSERT_EQ(GetHistoricalMaxOpId(tablets), OpId::Invalid());

  // Committed transactions should change max_op_id.
  ASSERT_OK(WriteRowsHelper(0, 100, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, true));

  OpId historical_max_op_id;
  ASSERT_OK(WaitFor(
      [&]() {
        historical_max_op_id = GetHistoricalMaxOpId(tablets);
        return historical_max_op_id > OpId::Invalid();
      },
      MonoDelta::FromSeconds(5),
      "historical_max_op_id should change"));

  // Restart all tservers.
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    test_cluster()->mini_tablet_server(i)->Shutdown();
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->Start());
  }

  // Should be same as before restart.
  ASSERT_OK(WaitFor(
      [&]() { return GetHistoricalMaxOpId(tablets) == historical_max_op_id; },
      MonoDelta::FromSeconds(30),
      "historical_max_op_id should be same as before restart"));
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKHistoricalMaxOpIdWithTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Should be -1.-1 in the beginning.
  ASSERT_EQ(GetHistoricalMaxOpId(tablets), OpId::Invalid());

  // Committed transactions should change max_op_id.
  ASSERT_OK(WriteRowsHelper(0, 100, &test_cluster_, true));
  OpId historical_max_op_id;
  ASSERT_OK(WaitFor(
      [&]() {
        historical_max_op_id = GetHistoricalMaxOpId(tablets);
        return historical_max_op_id > OpId::Invalid();
      },
      MonoDelta::FromSeconds(5),
      "historical_max_op_id should change"));

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  // Should be same as before split.
  OpId new_historical_max_op_id = GetHistoricalMaxOpId(tablets_after_first_split);
  ASSERT_EQ(new_historical_max_op_id, OpId::Invalid());
  new_historical_max_op_id = GetHistoricalMaxOpId(tablets_after_first_split, 1);
  ASSERT_EQ(new_historical_max_op_id, OpId::Invalid());

  ASSERT_OK(WriteRowsHelper(1000, 2000, &test_cluster_, true));
  ASSERT_OK(WaitFor(
      [&]() {
        return (GetHistoricalMaxOpId(tablets_after_first_split) > historical_max_op_id) &&
               (GetHistoricalMaxOpId(tablets_after_first_split, 1) > historical_max_op_id);
      },
      MonoDelta::FromSeconds(5),
      "historical_max_op_id should change"));
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(CDCSDKMultipleAlter)) {
  const int num_tservers = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
  const uint32_t num_tablets = 1;
  // Creates a table with a key, and value column.
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  // Create CDC stream.
  auto stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  ASSERT_OK(WriteRowsHelper(1 /* start */, 11 /* end */, &test_cluster_, true));
  // Call Getchanges
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  // Validate the columns and insert counts.
  ValidateColumnCounts(change_resp, 2);
  ValidateInsertCounts(change_resp, 10);

  for (int nonkey_column_count = 2; nonkey_column_count < 15; ++nonkey_column_count) {
    std::string added_column_name = "value_" + std::to_string(nonkey_column_count);
    ASSERT_OK(AddColumn(&test_cluster_, kNamespaceName, kTableName, added_column_name));
    ASSERT_OK(WriteRowsHelper(
        nonkey_column_count * 10 + 1 /* start */,
        nonkey_column_count * 10 + 11 /* end */,
        &test_cluster_,
        true,
        3,
        kTableName,
        {added_column_name}));
  }
  LOG(INFO) << "Added columns and pushed required records";
  constexpr size_t expected_records =
      13 * 10 + 13; /* number of add columns 'times' insert per batch + expected DDL records */

  std::vector<CDCSDKProtoRecordPB> seen_records;
  ASSERT_OK(WaitFor(
      [&]() -> bool {
        change_resp =
            EXPECT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));

        for (const auto& record : change_resp.cdc_sdk_proto_records()) {
          seen_records.push_back(record);
        }

        if (seen_records.size() >= expected_records) return true;
        return false;
      },
      MonoDelta::FromSeconds(120),
      "Did not get all the expected records"));
  LOG(INFO) << "Got all required records";

  uint seen_ddl_records = 0;
  for (const auto& record : seen_records) {
    if (record.row_message().op() == RowMessage::DDL) {
      seen_ddl_records += 1;
    } else if (record.row_message().op() == RowMessage::INSERT) {
      auto key_value = record.row_message().new_tuple(0).datum_int32();
      auto expected_column_count = std::ceil(key_value / 10.0);
      ASSERT_EQ(record.row_message().new_tuple_size(), expected_column_count);
    }
  }

  ASSERT_GE(seen_ddl_records, 13);
}

TEST_F(
    CDCSDKYsqlTest,
    YB_DISABLE_TEST_IN_TSAN(TestCDCSDKConsistentStreamWithRandomReqSafeTimeChanges)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_stream_records_threshold_size_bytes) = 64_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  auto stream_id = ASSERT_RESULT(CreateDBStream(EXPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

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

  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, false));

  auto get_changes_resp = GetAllPendingChangesWithRandomReqSafeTimeChanges(stream_id, tablets);
  std::unordered_set<int32_t> seen_unique_pk_values;
  for (auto record : get_changes_resp.records) {
    if (record.row_message().op() == RowMessage::INSERT) {
      const int32_t& pk_value = record.row_message().new_tuple(0).datum_int32();
      seen_unique_pk_values.insert(pk_value);
    }
  }

  ASSERT_EQ(seen_unique_pk_values.size(), 5000);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCDCSDKMakesProgressWithLongRunningTxn)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_resolve_intent_lag_threshold_ms) = 10 * 1000;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Flushed transactions are replayed only if there is a cdc stream.
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // Initiate a transaction with 'BEGIN' statement. But do not commit it.
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 0; i < 100; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_table VALUES ($0, $1)", i, i + 1));
  }

  // Commit another transaction while we still have the previous one open.
  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 1000, false));

  uint32 seen_insert_records = 0;
  auto update_insert_count = [&](const GetChangesResponsePB& change_resp) {
    for (const auto& record : change_resp.cdc_sdk_proto_records()) {
      if (record.row_message().op() == RowMessage::INSERT) {
        seen_insert_records += 1;
      }
    }
  };

  // Initially we will not see any records, even though we have a committed transaction, because the
  // running transaction holds back the consistent_safe_time.
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  update_insert_count(change_resp);
  ASSERT_EQ(seen_insert_records, 0);
  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  update_insert_count(change_resp);
  ASSERT_EQ(seen_insert_records, 0);

  // Eventually, after FLAGS_cdc_resolve_intent_lag_threshold_ms time we should see the records for
  // the committed transaction.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        change_resp =
            VERIFY_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
        update_insert_count(change_resp);
        if (seen_insert_records == 100) return true;

        return false;
      },
      MonoDelta::FromSeconds(30), "Did not see all expected records"));
}

}  // namespace cdc
}  // namespace yb
