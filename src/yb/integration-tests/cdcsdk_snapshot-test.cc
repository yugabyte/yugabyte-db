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

// Insert a row before snapshot. Insert a row after snapshot.
// Expected records: (DDL, READ) and (DDL, INSERT).
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(InsertBeforeAfterSnapshot)) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 0, 0, 1, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records_before_snapshot[] = {{0, 0}, {1, 2}};
  ExpectedRecord expected_records_after_snapshot[] = {{2, 3}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  GetChangesResponsePB change_resp_updated =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));

  uint32_t expected_record_count = 0;
  for (const auto& record : change_resp_updated.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecord(record, expected_records_before_snapshot[expected_record_count++], count);
  }

  ASSERT_OK(WriteRows(2 /* start */, 3 /* end */, &test_cluster_));
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
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(InsertSingleRowSnapshot)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRowsHelper(1 /* start */, 2 /* end */, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false,
      /* timeout_secs = */ 30, /* is_compaction = */ false));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 0, 0, 0, 1, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  GetChangesResponsePB change_resp_updated =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));

  uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count);
  }
  LOG(INFO) << "Got " << count[4] << " read record and " << count[0] << " ddl record";
  CheckCount(expected_count, count);
}

// Begin transaction, insert one row, commit transaction, update, enable snapshot
// Expected records: (DDL, READ).
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(UpdateInsertedRowSnapshot)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRowsHelper(1 /* start */, 2 /* end */, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false,
      /* timeout_secs = */ 30, /* is_compaction = */ false));
  ASSERT_OK(UpdateRows(1 /* key */, 1 /* value */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 0, 0, 0, 1, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 1}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  GetChangesResponsePB change_resp_updated =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));

  uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count);
  }
  LOG(INFO) << "Got " << count[4] << " read record and " << count[0] << " ddl record";
  CheckCount(expected_count, count);
}

// Begin transaction, insert one row, commit transaction, delete, enable snapshot
// Expected records: (DDL).
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(DeleteInsertedRowSnapshot)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRowsHelper(1 /* start */, 2 /* end */, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false,
      /* timeout_secs = */ 30, /* is_compaction = */ false));
  ASSERT_OK(DeleteRows(1 /* key */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 0, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  GetChangesResponsePB change_resp_updated =
      ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));

  uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count);
  }
  LOG(INFO) << "Got " << count[4] << " read record and " << count[0] << " ddl record";
  CheckCount(expected_count, count);
}

// Insert 10K rows using a thread and after a while enable snapshot.
// Expected sum of READs and INSERTs is 10K.
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(InsertBeforeDuringSnapshot)) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // 10K records inserted using a thread.
  std::vector<std::thread> threads;
  threads.emplace_back(
      [&]() { ASSERT_OK(WriteRows(1 /* start */, 10001 /* end */, &test_cluster_)); });
  SleepFor(MonoDelta::FromMilliseconds(100));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool end_snapshot = false;
  while (true) {
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        read_count++;
      } else if (record.row_message().op() == RowMessage::INSERT) {
        end_snapshot = true;
        break;
      }
    }
    if (end_snapshot) {
      break;
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
    if (reads_snapshot == 10000) {
      break;
    }
  }

  for (auto& t : threads) {
    t.join();
  }

  LOG(INFO) << "Insertion of records using threads has completed.";

  // Count the number of INSERTS.
  uint32_t inserts_snapshot = 0;
  while (true) {
    GetChangesResponsePB change_resp_after_snapshot =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size_after_snapshot = change_resp_after_snapshot.cdc_sdk_proto_records_size();
    if (record_size_after_snapshot == 0) {
      break;
    }
    uint32_t insert_count = 0;
    for (uint32_t i = 0; i < record_size_after_snapshot; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_after_snapshot.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::INSERT) {
        insert_count++;
      }
    }
    inserts_snapshot += insert_count;
    change_resp = change_resp_after_snapshot;
  }
  LOG(INFO) << "Got " << reads_snapshot + inserts_snapshot << " total (read + insert) record";
  ASSERT_EQ(reads_snapshot + inserts_snapshot, 10000);
}

// Insert 10K rows using a thread and after a while enable snapshot.
// After snapshot completes, insert 10K rows using threads.
// Expected sum of READs and INSERTs is 20K.
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(InsertBeforeDuringAfterSnapshot)) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // 10K records inserted using a thread.
  std::vector<std::thread> threads;
  threads.emplace_back(
      [&]() { ASSERT_OK(WriteRows(1 /* start */, 10001 /* end */, &test_cluster_)); });
  SleepFor(MonoDelta::FromMilliseconds(100));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool end_snapshot = false;
  while (true) {
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        read_count++;
      } else if (record.row_message().op() == RowMessage::INSERT) {
        end_snapshot = true;
        break;
      }
    }
    if (end_snapshot) {
      break;
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
    if (reads_snapshot == 10000) {
      break;
    }
  }

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
  uint32_t inserts_snapshot = 0;
  while (true) {
    GetChangesResponsePB change_resp_after_snapshot =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size_after_snapshot = change_resp_after_snapshot.cdc_sdk_proto_records_size();
    if (record_size_after_snapshot == 0) {
      break;
    }
    uint32_t insert_count = 0;
    for (uint32_t i = 0; i < record_size_after_snapshot; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_after_snapshot.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::INSERT) {
        insert_count++;
      }
    }
    inserts_snapshot += insert_count;
    change_resp = change_resp_after_snapshot;
  }
  LOG(INFO) << "Got " << reads_snapshot + inserts_snapshot << " total (read + insert) record";
  ASSERT_EQ(reads_snapshot + inserts_snapshot, 20000);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSnapshotWithInvalidFromOpId)) {
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  change_resp.mutable_cdc_sdk_checkpoint()->set_index(-1);
  change_resp.mutable_cdc_sdk_checkpoint()->set_term(-1);
  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  while (true) {
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCompactionDuringSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  // Table having key:value_1 column
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_update = true;
  while (true) {
    if (do_update) {
      ASSERT_OK(UpdateRows(200, 2001, &test_cluster_));
      ASSERT_OK(DeleteRows(1, &test_cluster_));
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 5;
      ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
      do_update = false;
    }
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    if (record_size == 0) {
      break;
    }
    uint32_t read_count = 0;
    vector<int> excepted_result(2);
    vector<int> actual_result(2);
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      std::stringstream s;

      if (record.row_message().op() == RowMessage::READ) {
        for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
          s << " " << record.row_message().new_tuple(jdx).datum_int32();
          actual_result[jdx] = record.row_message().new_tuple(jdx).datum_int32();
        }
        LOG(INFO) << "row: " << i << " : " << s.str();
        // we should only get row values w.r.t snapshot, not changed values during snapshot.
        if (actual_result[0] == 200) {
          excepted_result[0] = 200;
          excepted_result[1] = 201;
          ASSERT_EQ(actual_result, excepted_result);
        } else if (actual_result[0] == 1) {
          excepted_result[0] = 1;
          excepted_result[1] = 2;
          ASSERT_EQ(actual_result, excepted_result);
        }
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
  }
  ASSERT_EQ(reads_snapshot, 200);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestMultipleTableAlterWithSnapshot)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  std::vector<std::string> expected_columns{kKeyColumnName, kValueColumnName, kValue4ColumnName};
  while (true) {
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestLeadershipChangeDuringSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_change_leader = true;
  while (true) {
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestServerFailureDuringSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_single_record_update) = false;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = EXPECT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  // Table having key:value_1 column
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_snapshot_failure = true;
  while (true) {
    auto result = UpdateCheckpoint(stream_id, tablets, &change_resp);
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

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(InsertedRowInbetweenSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Invalid()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  ASSERT_OK(WriteRows(101 /* start */, 201 /* end */, &test_cluster_));
  int count = 0;
  uint32_t record_size = 0;
  GetChangesResponsePB change_resp_updated;
  while (true) {
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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
  CDCStateTable cdc_state_table(test_client());
  Status s;
  for (auto row_result : ASSERT_RESULT(
           cdc_state_table.GetTableRange(CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    auto checkpoint = *row.checkpoint;
    if (row.key.tablet_id == tablets[0].tablet_id()) {
      LOG(INFO) << "Read cdc_state table with tablet_id: " << row.key.tablet_id
                << " stream_id: " << row.key.stream_id << " checkpoint is: " << checkpoint;
      ASSERT_GT(checkpoint.term, 0);
      ASSERT_GT(checkpoint.index, 0);
    }
  }
  ASSERT_OK(s);

  set_resp = ASSERT_RESULT(SetCDCCheckpoint(
      stream_id, tablets,
      OpId(
          change_resp_updated.cdc_sdk_checkpoint().term(),
          change_resp_updated.cdc_sdk_checkpoint().index())));

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

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestStreamActiveWithSnapshot)) {
  // This testcase is to verify during snapshot operation, active time needs to be updated in
  // cdc_state table, so that stream should not expire if the snapshot operation takes longer than
  // the stream expiry time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_intent_retention_ms) = 20000;  // 20 seconds
  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Inserting 1000 rows, so that there will be 100 snapshot batches each with
  // 'FLAGS_cdc_snapshot_batch_size'(10) rows.
  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  int count = 0;
  GetChangesResponsePB change_resp_updated;
  // There will be atleast 100 calls to 'GetChanges', and we wait 1 second between each iteration.
  // If the active time wasn't updated during the process, 'GetChanges' would fail before we get all
  // data.
  while (true) {
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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
    SleepFor(MonoDelta::FromSeconds(1));
  }
  // We assert we got all the data after 100 iterations , which means the stream was active even
  // after ~100 seconds.
  ASSERT_EQ(count, 1000);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestLeadershipChangeAndSnapshotAffectsCheckpoint)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 1, &tablets,
      /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  SleepFor(MonoDelta::FromSeconds(10));

  ASSERT_OK(WriteRowsHelper(0 /* start */, 200 /* end */, &test_cluster_, true));

  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  auto change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  uint seen_record_count = 0;
  seen_record_count += change_resp.cdc_sdk_proto_records_size();
  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  seen_record_count += change_resp.cdc_sdk_proto_records_size();
  ASSERT_GE(seen_record_count, 200);

  auto checkpoint_after_last_record =
      OpId(change_resp.cdc_sdk_checkpoint().term(), change_resp.cdc_sdk_checkpoint().index());

  ASSERT_OK(CreateSnapshot(kNamespaceName));

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto result = GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint());
        if (!result.ok()) {
          return false;
        }
        change_resp = *result;
        auto checkpoint_after_snapshot =
            OpId(change_resp.cdc_sdk_checkpoint().term(), change_resp.cdc_sdk_checkpoint().index());
        return (checkpoint_after_snapshot > checkpoint_after_last_record);
      },
      MonoDelta::FromSeconds(120),
      "GetChanges did not see the record for snapshot"));

  auto checkpoint_after_snapshot =
      OpId(change_resp.cdc_sdk_checkpoint().term(), change_resp.cdc_sdk_checkpoint().index());
  ASSERT_GT(checkpoint_after_snapshot, checkpoint_after_last_record);

  size_t first_leader_index = -1;
  size_t first_follower_index = -1;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &first_leader_index, &first_follower_index);
  ASSERT_OK(ChangeLeaderOfTablet(first_follower_index, tablets[0].tablet_id()));

  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  auto checkpoint_after_leadership_change =
      OpId(change_resp.cdc_sdk_checkpoint().term(), change_resp.cdc_sdk_checkpoint().index());
  ASSERT_GT(checkpoint_after_leadership_change, checkpoint_after_snapshot);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCheckpointUpdatedDuringSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;

  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Invalid()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  int count = 0;
  GetChangesResponsePB change_resp_updated;

  uint64_t last_seen_snapshot_save_time = 0;
  std::string last_seen_snapshot_key = "";

  while (true) {
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();

    const auto& snapshopt_time_key_pair = ASSERT_RESULT(GetSnapshotDetailsFromCdcStateTable(
        stream_id, tablets.begin()->tablet_id(), test_client()));

    auto const& checkpoint_result =
        ASSERT_RESULT(GetCDCSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

    // Assert that 'GetCDCCheckpoint' return the same snapshot_time and key as in 'cdc_state' table.
    ASSERT_EQ(checkpoint_result.snapshot_time(), std::get<0>(snapshopt_time_key_pair));
    ASSERT_EQ(checkpoint_result.snapshot_key(), std::get<1>(snapshopt_time_key_pair));

    if (last_seen_snapshot_save_time != 0) {
      // Assert that the snapshot save time does not change per 'GetChanges' call.
      ASSERT_EQ(last_seen_snapshot_save_time, std::get<0>(snapshopt_time_key_pair));
    }
    last_seen_snapshot_save_time = std::get<0>(snapshopt_time_key_pair);
    ASSERT_NE(last_seen_snapshot_save_time, 0);

    if (!last_seen_snapshot_key.empty()) {
      // Assert that the snapshot key is updated per 'GetChanges' call.
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
  ASSERT_EQ(checkpoint_result.snapshot_key(), "");
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSnapshotNoData)) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  // We are calling 'GetChanges' in snapshot mode, but sine there is no data in the tablet, the
  // first response itself should indicate the end of snapshot.
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  // 'write_id' must be set to 0, 'key' must to empty, to indicate that the snapshot is done.
  ASSERT_EQ(change_resp.cdc_sdk_checkpoint().write_id(), 0);
  ASSERT_EQ(change_resp.cdc_sdk_checkpoint().key(), "");

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  ASSERT_GT(change_resp.cdc_sdk_proto_records_size(), 1000);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSnapshotForColocatedTablet)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_update_local_peer_min_index) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  const int64_t snapshot_recrods_per_table = 500;
  for (int i = 0; i < snapshot_recrods_per_table; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1, $2)", i, i + 1, i + 2));
    ASSERT_OK(
        conn.ExecuteFormat("INSERT INTO test2 VALUES ($0, $1, $2, $3)", i, i + 1, i + 2, i + 3));
  }

  auto verify_all_snapshot_records = [&](GetChangesResponsePB& initial_change_resp,
                                         const TableId& req_table_id, const TableName& table_name) {
    bool first_call = true;
    int64_t seen_snapshot_records = 0;
    GetChangesResponsePB change_resp;
    while (true) {
      if (first_call) {
        change_resp =
            ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &initial_change_resp, req_table_id));
        first_call = false;
      } else {
        change_resp =
            ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp, req_table_id));
      }

      for (const auto& record : change_resp.cdc_sdk_proto_records()) {
        if (record.row_message().op() == RowMessage::READ) {
          seen_snapshot_records += 1;
          ASSERT_EQ(record.row_message().table(), table_name);
        }
      }

      if (change_resp.cdc_sdk_checkpoint().key().empty() &&
          change_resp.cdc_sdk_checkpoint().write_id() == 0 &&
          change_resp.cdc_sdk_checkpoint().snapshot_time() == 0) {
        ASSERT_EQ(seen_snapshot_records, snapshot_recrods_per_table);
        break;
      }
    }
  };

  auto req_table_id = GetColocatedTableId("test1");
  ASSERT_NE(req_table_id, "");
  // Assert that we get all records from the second table: "test1".
  GetChangesResponsePB initial_change_resp =
      ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  GetChangesResponsePB change_resp;
  verify_all_snapshot_records(initial_change_resp, req_table_id, "test1");
  LOG(INFO) << "Verified snapshot records for table: test1";

  // Assert that we get all records from the second table: "test2".
  req_table_id = GetColocatedTableId("test2");
  ASSERT_NE(req_table_id, "");
  verify_all_snapshot_records(initial_change_resp, req_table_id, "test2");
  LOG(INFO) << "Verified snapshot records for table: test2";
}

TEST_F(
    CDCSDKYsqlTest,
    YB_DISABLE_TEST_IN_TSAN(TestCommitTimeRecordTimeAndNoSafepointRecordForSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 10;

  ASSERT_OK(SetUpWithParams(1, 1, false, true /* cdc_populate_safepoint_record */));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Invalid()));
  ASSERT_FALSE(set_resp.has_error());

  // Commit a transaction with 1000 rows.
  ASSERT_OK(WriteRowsHelper(1 /* start */, 1001 /* end */, &test_cluster_, true));

  // Insert 1000 single shard transactions
  ASSERT_OK(WriteRows(1001 /* start */, 2001 /* end */, &test_cluster_));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  int count = 0;
  GetChangesResponsePB change_resp_updated;
  while (true) {
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));

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

TEST_F(
    CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestGetCheckpointOnAddedColocatedTableWithNoSnapshot)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  const int64_t snapshot_recrods_per_table = 100;
  for (int i = 0; i < snapshot_recrods_per_table; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test1 VALUES ($0, $1, $2)", i, i + 1, i + 2));
  }

  auto req_table_id = GetColocatedTableId("test1");
  ASSERT_NE(req_table_id, "");
  auto change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets, req_table_id));
  while (true) {
    change_resp = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp, req_table_id));

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

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSnapshotRecordSnapshotKey)) {
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  FLAGS_cdc_snapshot_batch_size = 10;

  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Invalid()));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 1001 /* end */, &test_cluster_));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  int count = 0;
  GetChangesResponsePB change_resp_updated;

  while (true) {
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));

    for (int32_t i = 0; i < change_resp_updated.cdc_sdk_proto_records_size(); ++i) {
      const CDCSDKProtoRecordPB& record = change_resp_updated.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::READ) {
        count += 1;
        ASSERT_EQ(record.cdc_sdk_op_id().term(), change_resp.cdc_sdk_checkpoint().term());
        ASSERT_EQ(record.cdc_sdk_op_id().index(), change_resp.cdc_sdk_checkpoint().index());
        ASSERT_EQ(record.cdc_sdk_op_id().write_id_key(), change_resp.cdc_sdk_checkpoint().key());
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
}

}  // namespace cdc
}  // namespace yb
