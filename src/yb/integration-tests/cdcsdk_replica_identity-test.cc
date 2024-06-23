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
#include "yb/master/sys_catalog_initialization.h"

namespace yb {
namespace cdc {

class CDCSDKReplicaIdentityTest : public CDCSDKYsqlTest {
 public:
  // TODO(#21681): This function is almost a duplicate of the corresponding function from the test
  // base. It should de-duped.
  Status SetUpWithParams(
      uint32_t replication_factor, uint32_t num_masters, bool colocated,
      bool cdc_populate_safepoint_record = false) {
    master::SetDefaultInitialSysCatalogSnapshotFlags();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_hide_pg_catalog_table_creation_logs) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pggate_rpc_timeout_secs) = 120;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = replication_factor;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_pack_full_row_update) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_populate_safepoint_record) = cdc_populate_safepoint_record;
    // Set max_replication_slots to a large value so that we don't run out of them during tests
    // and don't have to do cleanups after every test case.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_replication_slots) = 500;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allowed_preview_flags_csv) = "ysql_yb_enable_replica_identity";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;

    MiniClusterOptions opts;
    opts.num_masters = num_masters;
    opts.num_tablet_servers = replication_factor;
    opts.cluster_id = "cdcsdk_cluster";

    test_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);

    RETURN_NOT_OK(test_cluster()->StartSync());
    RETURN_NOT_OK(test_cluster()->WaitForTabletServerCount(replication_factor));
    RETURN_NOT_OK(WaitForInitDb(test_cluster()));
    test_cluster_.client_ = VERIFY_RESULT(test_cluster()->CreateClient());
    RETURN_NOT_OK(InitPostgres(&test_cluster_));
    RETURN_NOT_OK(CreateDatabase(&test_cluster_, kNamespaceName, colocated));

    cdc_proxy_ = GetCdcProxy();

    LOG(INFO) << "Cluster created successfully for CDCSDK";
    return Status::OK();
  }

  void WriteUpdateDelete(std::string table_name = kTableName) {
    ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_, 2, table_name));
    ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_, table_name));
    ASSERT_OK(DeleteRows(1, &test_cluster_, table_name));
  }

  Status ValidateReplicaIdentityRecords(
      const GetChangesResponsePB& change_resp, ExpectedRecord* expected_records,
      ExpectedRecord* expected_before_image_records, uint32* count, const std::string& table_name,
      bool is_nothing_record = false) {
    uint32_t seen_dml_records = 0;
    for (const auto& record : change_resp.cdc_sdk_proto_records()) {
      if (record.row_message().op() == RowMessage::BEGIN ||
          record.row_message().op() == RowMessage::COMMIT) {
        continue;
      }
      if (record.row_message().op() == RowMessage::DDL) {
        count[0]++;
        continue;
      }

      CheckRecord(
          record, expected_records[seen_dml_records], count, true,
          expected_before_image_records[seen_dml_records], table_name, is_nothing_record);
      seen_dml_records++;
    }
    return Status::OK();
  }
};

// This test verifies that only the last replica identity change before stream creation is
// refelected in CDC records. The replica identity changes after the stream creation are not
// reflected in the records for the stream.
TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestReplicaIdentityWithStreamCreation)) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY NOTHING"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY DEFAULT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id_1 = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_1, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // These alter replica identity will have no effect on records streamed for stream_id_1
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY DEFAULT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY CHANGE"));

  // stream_id_2 will have replica identity CHANGE for test_table
  xrepl::StreamId stream_id_2 = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_2, tablets));
  ASSERT_FALSE(set_resp.has_error());

  WriteUpdateDelete(kTableName);

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {10, 2, 2, 2, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {10, 4, 0, 2, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records_1[] = {{1, 2}, {1, 3}, {1, 3}};
  ExpectedRecord expected_before_image_records_1[] = {{}, {1, 2}, {1, 3}};

  ExpectedRecord expected_records_2[] = {{1, 2}, {1, 3}, {1, INT_MAX}};
  ExpectedRecord expected_before_image_records_2[] = {{}, {}, {1, INT_MAX}};

  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets));
  ASSERT_OK(ValidateReplicaIdentityRecords(
      change_resp_1, expected_records_1, expected_before_image_records_1, count, kTableName));

  GetChangesResponsePB change_resp_2 = ASSERT_RESULT(GetChangesFromCDC(stream_id_2, tablets));
  ASSERT_OK(ValidateReplicaIdentityRecords(
      change_resp_2, expected_records_2, expected_before_image_records_2, count, kTableName));

  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestSingleShardReplicaIdentityFull)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));
  ASSERT_OK(DeleteRows(1, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 2, 1, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 3, 0, 1, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {1, 3}, {1, 4}, {1, 4}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {1, 3}, {1, 4}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_valid_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }

    CheckRecord(
        record, expected_records[seen_valid_records], count, true,
        expected_before_image_records[seen_valid_records]);
    seen_valid_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestSingleShardReplicaIdentityChange)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false /* colocated */));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // The default replica identity for user created tables is CHANGE.
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(DeleteRows(1, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 1, 1, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 2, 0, 1, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {{}, {1, 2, 3}, {1, 3, INT_MAX}, {}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {}, {}, {}, {1, INT_MAX, INT_MAX}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecordWithThreeColumns(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestSingleShardReplicaIdentityDefault)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(SetUpWithParams(1, 1, false /* colocated */));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY DEFAULT"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(DeleteRows(1, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 1, 1, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 2, 0, 1, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {{}, {1, 2, 3}, {1, 3, 3}, {}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {}, {}, {}, {1, INT_MAX, INT_MAX}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecordWithThreeColumns(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestSingleShardReplicaIdentityNothing)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(SetUpWithParams(1, 1, false /* colocated */));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY NOTHING"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(DeleteRows(1, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 1, 1, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 2, 0, 1, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {{}, {1, 2, 3}, {1, 3, 3}, {}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {{}, {}, {}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecordWithThreeColumns(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records], false, true);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestMultiShardUpdateReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  std::multimap<uint32_t, uint32_t> col_val_map;
  col_val_map.insert({1, 88});
  col_val_map.insert({1, 888});

  ASSERT_OK(WriteAndUpdateRowsHelper(
      1 /* start */, 2 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  col_val_map.clear();
  col_val_map.insert({1, 999});
  col_val_map.insert({2, 99});
  ASSERT_OK(WriteAndUpdateRowsHelper(
      2 /* start */, 3 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 2, 4, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 6, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2},   {1, 88}, {1, 888},
                                       {2, 3}, {1, 999}, {2, 99}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {1, 2}, {}, {1, 888}, {2, 3}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }

    CheckRecord(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(
    CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestSingleMultiShardUpdateReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  std::multimap<uint32_t, uint32_t> col_val_map;
  col_val_map.insert({2, 88});
  col_val_map.insert({2, 888});

  ASSERT_OK(WriteAndUpdateRowsHelper(
      2 /* start */, 3 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  col_val_map.clear();
  col_val_map.insert({2, 999});
  col_val_map.insert({3, 99});
  ASSERT_OK(WriteAndUpdateRowsHelper(
      3 /* start */, 4 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 3, 6, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 9, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0},  {1, 2},   {1, 3}, {1, 4},   {2, 3},
                                       {2, 88}, {2, 888}, {3, 4}, {2, 999}, {3, 99}};
  ExpectedRecord expected_before_image_records[] = {{},     {0, 0}, {1, 2}, {1, 3},   {0, 0},
                                                    {2, 3}, {2, 3}, {0, 0}, {2, 888}, {3, 4}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecord(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(SingleShardUpdateMultiColumnReplicaIdentity)) {
  uint32_t num_cols = 3;
  map<std::string, uint32_t> col_val_map;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = EXPECT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, false, "", "public",
      num_cols));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 4 /* end */, &test_cluster_, num_cols));

  col_val_map.insert(pair<std::string, uint32_t>("col2", 9));
  col_val_map.insert(pair<std::string, uint32_t>("col3", 10));
  ASSERT_OK(UpdateRows(1 /* key */, col_val_map, &test_cluster_));
  ASSERT_OK(UpdateRows(2 /* key */, col_val_map, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 3, 2, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 5, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {{0, 0, 0}, {1, 2, 3},  {2, 3, 4},
                                                       {3, 4, 5}, {1, 9, 10}, {2, 9, 10}};

  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 2, 3}, {2, 3, 4}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecordWithThreeColumns(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records], true);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(
    CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestCompactionWithReplicaIdentityChange)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;
  // Testing compaction without compaction file filtering for TTL expiration.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion)";
  SleepFor(MonoDelta::FromSeconds(5));
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);

  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  auto count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  ASSERT_LT(count_after_compaction, count_before_compaction);
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestCompactionWithSnapshotAndReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot());
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  // Read from cdc_state once the snapshot is initiated.
  auto expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_NE((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);

  // Assert that the safe time is invalid in the tablet_peers
  AssertSafeTimeAsExpectedInTabletPeers(tablets[0].tablet_id(), (*expected_row).cdc_sdk_safe_time);

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_update = true;
  GetChangesResponsePB change_resp_updated;
  vector<int> excepted_result(2);
  vector<int> actual_result(2);
  while (true) {
    if (do_update) {
      ASSERT_OK(UpdateRows(100, 1001, &test_cluster_));
      ASSERT_OK(DeleteRows(1, &test_cluster_));
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
      ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
      do_update = false;
    }
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
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
          if (record.row_message().new_tuple(jdx).column_name() == kKeyColumnName) {
            actual_result[0] = record.row_message().new_tuple(jdx).datum_int32();
          } else if (record.row_message().new_tuple(jdx).column_name() == kValueColumnName) {
            actual_result[1] = record.row_message().new_tuple(jdx).datum_int32();
          }
        }
        LOG(INFO) << "row: " << i << " : " << s.str();
        // we should only get row values w.r.t snapshot, not changed values during snapshot.
        if (actual_result[0] == 100) {
          excepted_result[0] = 100;
          excepted_result[1] = 101;
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
  ASSERT_EQ(reads_snapshot, 100);
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
  auto result = test_cluster_.mini_cluster_->CompactTablets();

  ASSERT_OK(WriteRows(101 /* start */, 102 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(2 /* key */, 4 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(2 /* key */, 5 /* value */, &test_cluster_));

  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion): "
            << change_resp.cdc_sdk_proto_records_size();
  ASSERT_OK(UpdateRows(2 /* key */, 6 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(2 /* key */, 10 /* value */, &test_cluster_));
  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  int count_after_compaction;
  ASSERT_OK(WaitFor(
      [&]() {
        auto result = test_cluster_.mini_cluster_->CompactTablets();
        if (!result.ok()) {
          return false;
        }
        count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
        if (count_after_compaction <= count_before_compaction) {
          return true;
        }
        return false;
      },
      MonoDelta::FromSeconds(60), "Compaction is restricted for the stream."));
  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  ASSERT_LE(count_after_compaction, count_before_compaction);
  // Read from cdc_state once the stream is done, without before image.
  expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_NE((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);

  // Assert that the safe time is invalid in the tablet_peers
  AssertSafeTimeAsExpectedInTabletPeers(tablets[0].tablet_id(), (*expected_row).cdc_sdk_safe_time);
}

TEST_F(CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestColumnDropWithReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 2)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 3 WHERE key = 1"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_2 INT"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_2 = 4 WHERE key = 1"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP COLUMN value_2"));
  // Sleep to ensure that alter table is committed in docdb
  // TODO: (#21288) Remove the sleep once the best effort waiting mechanism for drop table lands.
  SleepFor(MonoDelta::FromSeconds(5));


  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {3, 1, 2, 0, 0, 0};
  const uint32_t packed_row_expected_count[] = {3, 2, 1, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {
      {0, 0, 0}, {1, 2, INT_MAX}, {1, 3, INT_MAX}, {0, 0, INT_MAX}, {1, 3, 4}, {}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {}, {}, {1, 2, INT_MAX}, {}, {1, 3, INT_MAX}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecordWithThreeColumns(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";

  CheckCount(FLAGS_ysql_enable_packed_row ? packed_row_expected_count : expected_count, count);
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestLargeTransactionUpdateRowsWithReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, false, "", "public", 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp =
      ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min(), kuint64max, false, 0, true));
  ASSERT_FALSE(set_resp.has_error());

  // Do batch insert into the table.
  uint32_t start_idx = 1;
  uint32_t end_idx = 1001;
  uint32_t batch_range = 1000;
  int batch_count = 4;
  for (int idx = 0; idx < batch_count; idx++) {
    ASSERT_OK(WriteRowsHelper(start_idx /* start */, end_idx /* end */, &test_cluster_, true, 3));
    start_idx = end_idx;
    end_idx += batch_range;
  }

  // Update all row where key is even
  ASSERT_OK(conn.Execute("UPDATE test_table set col2 = col2 + 1 where col1 % 2 = 0"));

  bool first_get_changes = true;
  GetChangesResponsePB change_resp;
  int insert_count = 0;
  int update_count = 0;
  while (true) {
    if (first_get_changes) {
      change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
      first_get_changes = false;

    } else {
      change_resp =
          ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    }

    if (change_resp.cdc_sdk_proto_records_size() == 0) {
      break;
    }
    ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        ASSERT_EQ(record.row_message().old_tuple_size(), 3);
        // Old tuples validations
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(1).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(2).datum_int32(), 0);
        // New tuples validations
        ASSERT_GT(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_LE(record.row_message().new_tuple(0).datum_int32(), 4000);
        ASSERT_GT(record.row_message().new_tuple(1).datum_int32(), 1);
        ASSERT_LE(record.row_message().new_tuple(1).datum_int32(), 4002);
        ASSERT_EQ(record.row_message().table(), kTableName);
        insert_count += 1;
      } else if (record.row_message().op() == RowMessage::UPDATE) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        ASSERT_EQ(record.row_message().old_tuple_size(), 3);
        // The old tuple key should match the new tuple key.
        ASSERT_EQ(
            record.row_message().old_tuple(0).datum_int32(),
            record.row_message().new_tuple(0).datum_int32());
        // The updated value of value_1 column should be more than 1 of its before image.
        ASSERT_EQ(
            record.row_message().new_tuple(1).datum_int32(),
            record.row_message().old_tuple(1).datum_int32() + 1);

        ASSERT_EQ(
            record.row_message().new_tuple(2).datum_int32(),
            record.row_message().old_tuple(2).datum_int32());

        ASSERT_EQ(record.row_message().table(), kTableName);
        update_count += 1;
      }
    }
  }
  LOG(INFO) << "Total insert count: " << insert_count << " update counts: " << update_count;
  ASSERT_EQ(insert_count, 2 * update_count);
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestLargeTransactionDeleteRowsWithReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp =
      ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min(), kuint64max, false, 0, true));
  ASSERT_FALSE(set_resp.has_error());

  // Do batch insert into the table.
  uint32_t start_idx = 1;
  uint32_t end_idx = 1001;
  uint32_t batch_range = 1000;
  int batch_count = 4;
  for (int idx = 0; idx < batch_count; idx++) {
    ASSERT_OK(WriteRowsHelper(start_idx /* start */, end_idx /* end */, &test_cluster_, true));
    start_idx = end_idx;
    end_idx += batch_range;
  }

  // Delete all rows where key is even.
  ASSERT_OK(conn.Execute("DELETE from test_table where key % 2 = 0"));

  bool first_get_changes = true;
  GetChangesResponsePB change_resp;
  int insert_count = 0;
  int delete_count = 0;
  while (true) {
    if (first_get_changes) {
      change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
      first_get_changes = false;

    } else {
      change_resp =
          ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    }

    if (change_resp.cdc_sdk_proto_records_size() == 0) {
      break;
    }
    ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 2);
        ASSERT_EQ(record.row_message().old_tuple_size(), 2);
        // Old tuples validations
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(1).datum_int32(), 0);

        // New tuples validations
        ASSERT_GT(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_LE(record.row_message().new_tuple(0).datum_int32(), 4000);
        ASSERT_GT(record.row_message().new_tuple(1).datum_int32(), 1);
        ASSERT_LE(record.row_message().new_tuple(1).datum_int32(), 4002);
        ASSERT_EQ(record.row_message().table(), kTableName);
        insert_count += 1;
      } else if (record.row_message().op() == RowMessage::DELETE) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 2);
        ASSERT_EQ(record.row_message().old_tuple_size(), 2);
        // The old tuple key should match the new tuple key.
        ASSERT_GT(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_LE(record.row_message().old_tuple(0).datum_int32(), 4000);
        ASSERT_GT(record.row_message().old_tuple(1).datum_int32(), 1);
        ASSERT_LE(record.row_message().old_tuple(1).datum_int32(), 4002);

        ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().table(), kTableName);
        delete_count += 1;
      }
    }
  }
  LOG(INFO) << "Total insert count: " << insert_count << " update counts: " << delete_count;
  ASSERT_EQ(insert_count, 2 * delete_count);
}

TEST_F(
    CDCSDKReplicaIdentityTest, YB_DISABLE_TEST_IN_TSAN(TestMultipleTableAlterWithReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Enable packing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 1_KB;
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 2)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, 2)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (3, 2)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (4, 2)"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_2 INT"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 3 WHERE key = 1"));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {2, 4, 1, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};
  const uint32_t expected_count_packed_row[] = {2, 4, 1, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {
      {0, 0, INT_MAX}, {1, 2, INT_MAX}, {2, 2, INT_MAX}, {3, 2, INT_MAX},
      {4, 2, INT_MAX}, {0, 0, INT_MAX}, {1, 3, INT_MAX}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {}, {}, {}, {}, {}, {}, {1, 2, INT_MAX}};
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }

    CheckRecordWithThreeColumns(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  CheckCount(FLAGS_ysql_enable_packed_row ? expected_count_packed_row : expected_count, count);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;
  constexpr int kCompactionTimeoutSec = 60;
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false,
      /* timeout_secs = */ kCompactionTimeoutSec, /* is_compaction = */ true));

  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  // Disable packing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_2 = 4 WHERE key = 1"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_2 = 5 WHERE key = 1"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP COLUMN value_2"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 5 WHERE key = 1"));
  ASSERT_OK(conn.Execute("DELETE FROM test_table WHERE key = 1"));
  // Sleep to ensure that alter table is committed in docdb
  // TODO: (#21288) Remove the sleep once the best effort waiting mechanism for drop table lands.
  SleepFor(MonoDelta::FromSeconds(5));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count_2[] = {1, 0, 3, 1, 0, 0};
  const uint32_t expected_count_packed_row_2[] = {1, 1, 2, 1, 0, 0};
  uint32_t count_2[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records_2[] = {
      {1, 3, 4}, {1, 3, 5}, {0, 0, INT_MAX}, {1, 5, INT_MAX}, {1, INT_MAX, INT_MAX}};
  ExpectedRecordWithThreeColumns expected_before_image_records_2[] = {
      {1, 3, INT_MAX}, {1, 3, 4}, {}, {1, 3, INT_MAX}, {1, 5, INT_MAX}};

  GetChangesResponsePB change_resp_2 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  uint32_t seen_ddl_records = 0;
  seen_dml_records = 0;
  for (const auto& record : change_resp_2.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT ||
        (seen_dml_records == 0 && seen_ddl_records == 0 &&
         record.row_message().op() == RowMessage::DDL)) {
      if (record.row_message().op() == RowMessage::DDL) {
        seen_ddl_records++;
      }
      continue;
    }

    CheckRecordWithThreeColumns(
        record, expected_records_2[seen_dml_records], count_2, true,
        expected_before_image_records_2[seen_dml_records]);
    if (record.row_message().op() == RowMessage::DDL) {
      seen_ddl_records++;
    }
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count_2[1] << " insert record and " << count_2[2] << " update record";
  CheckCount(
      FLAGS_ysql_enable_packed_row ? expected_count_packed_row_2 : expected_count_2, count_2);
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestSingleShardUpdateOnColocatedTableWithReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, true /* colocated */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test_table(key int PRIMARY KEY, value_1 int);"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 2, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 3, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {1, 3}, {1, 4}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {1, 3}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    CheckRecord(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestMultipleColocatedTablesWithDifferentReplicaIdentities)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, true /* colocated */));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table_1 (key int PRIMARY KEY, value_1 int)"));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table_2 (key int primary key, value_1 int)"));

  auto table_1 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table_1"));
  auto table_2 = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, "test_table_2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table_1, 0, &tablets, nullptr));

  ASSERT_EQ(tablets.size(), 1);

  ASSERT_OK(conn.Execute("ALTER TABLE test_table_1 REPLICA IDENTITY FULL"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table_2 REPLICA IDENTITY DEFAULT"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(conn.Execute("INSERT INTO test_table_1 VALUES (1,1)"));
  ASSERT_OK(conn.Execute("UPDATE test_table_1 set value_1 = 2 where key = 1"));
  ASSERT_OK(conn.Execute("DELETE FROM test_table_1 where key = 1"));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {2, 2, 2, 2, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {2, 4, 0, 2, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records_1[] = {{1, 1}, {1, 2}, {1, 2}};
  ExpectedRecord expected_before_image_records_1[] = {{}, {1, 1}, {1, 2}};

  ExpectedRecord expected_records_2[] = {{1, 1}, {1, 2}, {1, INT_MAX}};
  ExpectedRecord expected_before_image_records_2[] = {{}, {}, {1, INT_MAX}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    if (record.row_message().op() == RowMessage::DDL) {
      count[0]++;
      continue;
    }

    CheckRecord(
        record, expected_records_1[seen_dml_records], count, true,
        expected_before_image_records_1[seen_dml_records], "test_table_1");
    seen_dml_records++;
  }

  ASSERT_OK(conn.Execute("INSERT INTO test_table_2 VALUES (1,1)"));
  ASSERT_OK(conn.Execute("UPDATE test_table_2 set value_1 = 2 where key = 1"));
  ASSERT_OK(conn.Execute("DELETE FROM test_table_2 where key = 1"));

  GetChangesResponsePB change_resp_2 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));

  seen_dml_records = 0;
  for (const auto& record : change_resp_2.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }
    if (record.row_message().op() == RowMessage::DDL) {
      count[0]++;
      continue;
    }
    CheckRecord(
        record, expected_records_2[seen_dml_records], count, true,
        expected_before_image_records_2[seen_dml_records], "test_table_2");
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";

  CheckCount(FLAGS_ysql_enable_packed_row ? expected_count_with_packed_row : expected_count, count);
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestMultiShardUpdateOnColocatedTableWithReplicaIdentity)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test_table(key int PRIMARY KEY, value_1 int);"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  std::multimap<uint32_t, uint32_t> col_val_map;
  col_val_map.insert({1, 88});
  col_val_map.insert({1, 888});

  ASSERT_OK(WriteAndUpdateRowsHelper(
      1 /* start */, 2 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  col_val_map.clear();
  col_val_map.insert({1, 999});
  col_val_map.insert({2, 99});
  ASSERT_OK(WriteAndUpdateRowsHelper(
      2 /* start */, 3 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 2, 4, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 6, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2},   {1, 88}, {1, 888},
                                       {2, 3}, {1, 999}, {2, 99}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {1, 2}, {}, {1, 888}, {2, 3}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t seen_dml_records = 0;
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    if (record.row_message().op() == RowMessage::BEGIN ||
        record.row_message().op() == RowMessage::COMMIT) {
      continue;
    }

    CheckRecord(
        record, expected_records[seen_dml_records], count, true,
        expected_before_image_records[seen_dml_records]);
    seen_dml_records++;
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(
    CDCSDKReplicaIdentityTest,
    YB_DISABLE_TEST_IN_TSAN(TestTablesWithDifferentReplicaIdentitiesInSameStream)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false /* colocated */));

  vector<std::string> table_sufix = {"_1", "_2", "_3", "_4"};
  vector<std::string> table_names;
  vector<YBTableName> tables;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  for (auto sufix : table_sufix) {
    table_names.push_back(kTableName + sufix);
    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName + sufix));
    tables.emplace_back(table);
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_for_table;
    ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_for_table, nullptr));
    ASSERT_EQ(tablets_for_table.size(), 1);
    *tablets.Add() = tablets_for_table[0];
  }
  ASSERT_EQ(tablets.size(), 4);

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table_1 REPLICA IDENTITY FULL"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table_3 REPLICA IDENTITY DEFAULT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table_4 REPLICA IDENTITY NOTHING"));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  for (int i = 0; i < 4; i++) {
    auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min(), 0, true, i));
    ASSERT_FALSE(set_resp.has_error());
  }

  WriteUpdateDelete(table_names[0]);

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {4, 4, 4, 4, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {4, 8, 0, 4, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records_1[] = {{1, 2}, {1, 3}, {1, 3}};
  ExpectedRecord expected_before_image_records_1[] = {{}, {1, 2}, {1, 3}};

  ExpectedRecord expected_records_2[] = {{1, 2}, {1, 3}, {1, INT_MAX}};
  ExpectedRecord expected_before_image_records_2[] = {{}, {}, {1, INT_MAX}};

  ExpectedRecord expected_records_3[] = {{1, 2}, {1, 3}, {1, INT_MAX}};
  ExpectedRecord expected_before_image_records_3[] = {{}, {}, {1, INT_MAX}};

  ExpectedRecord expected_records_4[] = {{1, 2}, {1, 3}, {}};
  ExpectedRecord expected_before_image_records_4[] = {{}, {}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  ASSERT_OK(ValidateReplicaIdentityRecords(
      change_resp, expected_records_1, expected_before_image_records_1, count, table_names[0]));

  WriteUpdateDelete(table_names[1]);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, nullptr, 1));
  ASSERT_OK(ValidateReplicaIdentityRecords(
      change_resp, expected_records_2, expected_before_image_records_2, count, table_names[1]));

  WriteUpdateDelete(table_names[2]);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, nullptr, 2));
  ASSERT_OK(ValidateReplicaIdentityRecords(
      change_resp, expected_records_3, expected_before_image_records_3, count, table_names[2]));

  WriteUpdateDelete(table_names[3]);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, nullptr, 3));
  ASSERT_OK(ValidateReplicaIdentityRecords(
      change_resp, expected_records_4, expected_before_image_records_4, count, table_names[3],
      true));

  CheckCount(FLAGS_ysql_enable_packed_row ? expected_count_with_packed_row : expected_count, count);
}

} // namespace cdc
} // namespace yb
