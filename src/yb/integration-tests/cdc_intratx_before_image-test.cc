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

#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

namespace yb {
namespace cdc {

class CDCIntraTxBeforeImageTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    // These tests work on older RECORD_TYPE support, so we disable replica identity support here so
    // that the record_type mode is used.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  }
};

TEST_F(CDCIntraTxBeforeImageTest, YB_DISABLE_TEST_IN_TSAN(TestTxnUpdatesAndDeleteBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  // Create a table with primary key and 2 additional columns (total 3 columns)
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  // Discover tablets for the table
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create a stream and set an initial checkpoint
  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Begin a transaction that inserts, updates multiple columns, then deletes the row and commits
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 11 WHERE $2 = 1", kTableName, kValueColumnName, kKeyColumnName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 22 WHERE $2 = 1", kTableName, kValue2ColumnName, kKeyColumnName));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 1", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Fetch changes from CDC and log them for verification
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  // Validate old/new values for each DML using CheckRecordWithThreeColumns.
  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_insert{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u1_new{1, 11, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u1_old{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u2_new{1, 11, 22};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u2_old{1, 11, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_del_old{1, 11, 22};

  int update_step = 0;
  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        CheckRecordWithThreeColumns(rec, exp_insert, count);
        break;
      case RowMessage::UPDATE:
        if (update_step == 0) {
          CheckRecordWithThreeColumns(
              rec, exp_u1_new, count, /*validate_old_tuple=*/true, exp_u1_old);
        } else if (update_step == 1) {
          CheckRecordWithThreeColumns(
              rec, exp_u2_new, count, /*validate_old_tuple=*/true, exp_u2_old);
        }
        update_step++;
        break;
      case RowMessage::DELETE:
        CheckRecordWithThreeColumns(
            rec, /*expected_records (unused)*/ exp_del_old, count,
            /*validate_old_tuple=*/true, exp_del_old);
        break;
      default:
        break;
    }
  }
}

// Test scenario: Update across multiple transactions, then delete
TEST_F(
    CDCIntraTxBeforeImageTest, YB_DISABLE_TEST_IN_TSAN(TestAcrossTxnUpdatesAndDeleteBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Transaction 1: INSERT
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 2: UPDATE value_1
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 11 WHERE $2 = 1", kTableName, kValueColumnName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 3: UPDATE value_2
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 22 WHERE $2 = 1", kTableName, kValue2ColumnName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 4: DELETE
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 1", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Fetch and validate changes
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_insert{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u1_new{1, 11, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u1_old{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u2_new{1, 11, 22};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u2_old{1, 11, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_del_old{1, 11, 22};

  int update_count = 0;
  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        CheckRecordWithThreeColumns(rec, exp_insert, count);
        break;
      case RowMessage::UPDATE:
        if (update_count == 0) {
          CheckRecordWithThreeColumns(
              rec, exp_u1_new, count, /*validate_old_tuple=*/true, exp_u1_old);
        } else if (update_count == 1) {
          CheckRecordWithThreeColumns(
              rec, exp_u2_new, count, /*validate_old_tuple=*/true, exp_u2_old);
        }
        update_count++;
        break;
      case RowMessage::DELETE:
        CheckRecordWithThreeColumns(
            rec, exp_del_old, count, /*validate_old_tuple=*/true, exp_del_old);
        break;
      default:
        break;
    }
  }
}

// Test scenario: Multiple updates on different columns in single transaction
TEST_F(
    CDCIntraTxBeforeImageTest,
    YB_DISABLE_TEST_IN_TSAN(TestIntraTxnMultipleColumnUpdatesBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;

  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Single transaction with INSERT and multiple updates on both columns
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 11, $2 = 22 WHERE $3 = 1", kTableName, kValueColumnName,
      kValue2ColumnName, kKeyColumnName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 12 WHERE $2 = 1", kTableName, kValueColumnName, kKeyColumnName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 23 WHERE $2 = 1", kTableName, kValue2ColumnName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_insert{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u1_new{1, 11, 22};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u1_old{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u2_new{1, 12, 22};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u2_old{1, 11, 22};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u3_new{1, 12, 23};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_u3_old{1, 12, 22};

  // Check if packed row marking is enabled
  // With packed row enabled: UPDATE of all columns comes as INSERT
  // Expected: 1 INSERT (initial) + 1 INSERT (UPDATE of all columns) + 2 UPDATEs (partial updates)
  // Without packed row: all updates come as UPDATE records
  // Expected: 1 INSERT + 3 UPDATEs
  bool packed_row_enabled = FLAGS_ysql_enable_packed_row;

  int insert_count = 0;
  int update_count = 0;
  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        if (insert_count == 0) {
          // Initial INSERT
          CheckRecordWithThreeColumns(rec, exp_insert, count);
        } else if (packed_row_enabled && insert_count == 1) {
          // First UPDATE (all columns) comes as INSERT with before image when packed row enabled
          CheckRecordWithThreeColumns(
              rec, exp_u1_new, count, /*validate_old_tuple=*/true, exp_u1_old);
        }
        insert_count++;
        break;
      case RowMessage::UPDATE:
        // With packed row: Remaining UPDATEs (partial column updates)
        // Without packed row: All UPDATEs
        if (update_count == 0) {
          CheckRecordWithThreeColumns(
              rec, packed_row_enabled ? exp_u2_new : exp_u1_new, count,
              /*validate_old_tuple=*/true, packed_row_enabled ? exp_u2_old : exp_u1_old);
        } else if (update_count == 1) {
          CheckRecordWithThreeColumns(
              rec, packed_row_enabled ? exp_u3_new : exp_u2_new, count,
              /*validate_old_tuple=*/true, packed_row_enabled ? exp_u3_old : exp_u2_old);
        } else if (update_count == 2 && !packed_row_enabled) {
          CheckRecordWithThreeColumns(
              rec, exp_u3_new, count, /*validate_old_tuple=*/true, exp_u3_old);
        }
        update_count++;
        break;
      default:
        break;
    }
  }

  // Verify counts based on whether packed row is enabled
  if (packed_row_enabled) {
    ASSERT_EQ(count[1], 2);  // 2 INSERTs (initial + all-column update)
    ASSERT_EQ(count[2], 2);  // 2 UPDATEs (partial updates)
  } else {
    ASSERT_EQ(count[1], 1);  // 1 INSERT
    ASSERT_EQ(count[2], 3);  // 3 UPDATEs
  }
}

// Test scenario: Insert, delete, re-insert same key within transaction
TEST_F(
    CDCIntraTxBeforeImageTest,
    YB_DISABLE_TEST_IN_TSAN(TestIntraTxnInsertDeleteReinsertBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Transaction: INSERT -> DELETE -> INSERT same key with different values
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 1", kTableName, kKeyColumnName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 30, 40)", kTableName));
  ASSERT_OK(conn.Execute("COMMIT"));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_insert1{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_del_old{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_insert2{1, 30, 40};

  int insert_count = 0;
  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        if (insert_count == 0) {
          CheckRecordWithThreeColumns(rec, exp_insert1, count);
        } else if (insert_count == 1) {
          CheckRecordWithThreeColumns(rec, exp_insert2, count);
        }
        insert_count++;
        break;
      case RowMessage::DELETE:
        CheckRecordWithThreeColumns(
            rec, exp_del_old, count, /*validate_old_tuple=*/true, exp_del_old);
        break;
      default:
        break;
    }
  }
}

// Test scenario: Multiple rows with mixed operations in single transaction
TEST_F(CDCIntraTxBeforeImageTest, YB_DISABLE_TEST_IN_TSAN(TestIntraTxnMultipleRowsBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Transaction with multiple rows: INSERT row1, INSERT row2, UPDATE row1, DELETE row2
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 30, 40)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 11 WHERE $2 = 1", kTableName, kValueColumnName, kKeyColumnName));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 2", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  // Validate that we see all operations with correct before images
  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    int32_t key = 0;
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        key = rec.row_message().new_tuple(0).datum_int32();
        LOG(INFO) << "INSERT for key: " << key;
        count[1]++;
        break;
      case RowMessage::UPDATE:
        key = rec.row_message().new_tuple(0).datum_int32();
        LOG(INFO) << "UPDATE for key: " << key;
        ASSERT_EQ(key, 1);                                 // Only row 1 should have an update
        ASSERT_GT(rec.row_message().old_tuple_size(), 0);  // Should have before image
        count[2]++;
        break;
      case RowMessage::DELETE:
        key = rec.row_message().old_tuple(0).datum_int32();
        LOG(INFO) << "DELETE for key: " << key;
        ASSERT_EQ(key, 2);                                 // Only row 2 should be deleted
        ASSERT_GT(rec.row_message().old_tuple_size(), 0);  // Should have before image
        count[3]++;
        break;
      default:
        break;
    }
  }

  // Should have 2 inserts, 1 update, 1 delete
  ASSERT_EQ(count[1], 2);
  ASSERT_EQ(count[2], 1);
  ASSERT_EQ(count[3], 1);
}

// Test scenario: Update in txn1, delete in txn2 (across transactions)
TEST_F(
    CDCIntraTxBeforeImageTest, YB_DISABLE_TEST_IN_TSAN(TestAcrossTxnUpdateThenDeleteBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Transaction 1: INSERT and UPDATE
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 11, $2 = 22 WHERE $3 = 1", kTableName, kValueColumnName,
      kValue2ColumnName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 2: DELETE
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 1", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_insert{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_update_new{1, 11, 22};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_update_old{1, 10, 20};
  CDCSDKYsqlTest::ExpectedRecordWithThreeColumns exp_del_old{1, 11, 22};

  // Check if packed row marking is enabled
  // With packed row enabled: UPDATE of all columns comes as INSERT
  // Expected: 2 INSERTs (initial + all-column update) + 1 DELETE
  // Without packed row: UPDATE comes as UPDATE
  // Expected: 1 INSERT + 1 UPDATE + 1 DELETE
  bool packed_row_enabled = FLAGS_ysql_enable_packed_row;

  int insert_count = 0;
  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        if (insert_count == 0) {
          // Initial INSERT
          CheckRecordWithThreeColumns(rec, exp_insert, count);
        } else if (packed_row_enabled && insert_count == 1) {
          // UPDATE of all columns comes as INSERT when packed row enabled
          CheckRecordWithThreeColumns(
              rec, exp_update_new, count, /*validate_old_tuple=*/true, exp_update_old);
        }
        insert_count++;
        break;
      case RowMessage::UPDATE:
        // Only when packed row is not enabled, UPDATE comes as UPDATE
        CheckRecordWithThreeColumns(
            rec, exp_update_new, count, /*validate_old_tuple=*/true, exp_update_old);
        break;
      case RowMessage::DELETE:
        CheckRecordWithThreeColumns(
            rec, exp_del_old, count, /*validate_old_tuple=*/true, exp_del_old);
        break;
      default:
        break;
    }
  }

  // Verify counts based on whether packed row is enabled
  if (packed_row_enabled) {
    ASSERT_EQ(count[1], 2);  // 2 INSERTs (initial + all-column update)
    ASSERT_EQ(count[2], 0);  // 0 UPDATEs
  } else {
    ASSERT_EQ(count[1], 1);  // 1 INSERT
    ASSERT_EQ(count[2], 1);  // 1 UPDATE
  }
  ASSERT_EQ(count[3], 1);  // 1 DELETE (same for both cases)
}

// Test scenario: Multiple deletes across transactions
TEST_F(
    CDCIntraTxBeforeImageTest, YB_DISABLE_TEST_IN_TSAN(TestAcrossTxnMultipleDeletesBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 INT PRIMARY KEY, $2 INT, $3 INT)", kTableName, kKeyColumnName,
      kValueColumnName, kValue2ColumnName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  // Transaction 1: INSERT three rows
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10, 20)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 30, 40)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (3, 50, 60)", kTableName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 2: DELETE row 1
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 1", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 3: DELETE row 2
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 2", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Transaction 4: DELETE row 3
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = 3", kTableName, kKeyColumnName));
  ASSERT_OK(conn.Execute("COMMIT"));

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  for (const auto& record : change_resp.cdc_sdk_proto_records()) {
    LOG(INFO) << "Record: " << record.DebugString();
  }

  uint32_t count[6] = {0, 0, 0, 0, 0, 0};
  std::map<int32_t, CDCSDKYsqlTest::ExpectedRecordWithThreeColumns> expected_deletes = {
      {1, {1, 10, 20}}, {2, {2, 30, 40}}, {3, {3, 50, 60}}};

  for (const auto& rec : change_resp.cdc_sdk_proto_records()) {
    switch (rec.row_message().op()) {
      case RowMessage::INSERT:
        count[1]++;
        break;
      case RowMessage::DELETE: {
        int32_t key = rec.row_message().old_tuple(0).datum_int32();
        LOG(INFO) << "DELETE for key: " << key;
        ASSERT_GT(rec.row_message().old_tuple_size(), 0);  // Must have before image
        auto it = expected_deletes.find(key);
        ASSERT_NE(it, expected_deletes.end());
        CheckRecordWithThreeColumns(
            rec, it->second, count, /*validate_old_tuple=*/true, it->second);
        break;
      }
      default:
        break;
    }
  }

  // Verify we got 3 inserts and 3 deletes
  ASSERT_EQ(count[1], 3);  // 3 INSERTs
  ASSERT_EQ(count[3], 3);  // 3 DELETEs
}

}  // namespace cdc
}  // namespace yb
