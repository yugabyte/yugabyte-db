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
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/integral_types.h"
#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

#include "yb/cdc/cdc_state_table.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

class CDCSDKTabletSplitTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  }

  void TestIntentPersistencyAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestCheckpointPersistencyAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestTransactionInsertAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestGetChangesReportsTabletSplitErrorOnRetries(CDCCheckpointType checkpoint_type);

  void TestGetChangesAfterTabletSplitWithMasterShutdown(CDCCheckpointType checkpoint_type);

  void TestGetChangesOnChildrenOnSplit(CDCCheckpointType checkpoint_type);

  void TestGetChangesOnParentTabletAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestGetChangesMultipleStreamsTabletSplit(CDCCheckpointType checkpoint_type);

  void TestSetCDCCheckpointAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestCDCStateTableAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCAfterTabletSplitReported(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCBeforeTabletSplitReported(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCBootstrapWithTabletSplit(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCBootstrapWithTwoTabletSplits(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCWithTwoTabletSplits(CDCCheckpointType checkpoint_type);

  void TestTabletSplitOnAddedTableForCDC(CDCCheckpointType checkpoint_type);

  void TestTabletSplitOnAddedTableForCDCWithMasterRestart(CDCCheckpointType checkpoint_type);

  void TestTransactionCommitAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestTabletSplitBeforeBootstrapGetCheckpoint(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCWithOnlyOnePolledChild(CDCCheckpointType checkpoint_type);

  void TestRecordCountsAfterMultipleTabletSplits(CDCCheckpointType checkpoint_type);

  void TestRecordCountAfterMultipleTabletSplitsInMultiNodeCluster(
      CDCCheckpointType checkpoint_type);

  void TestStreamMetaDataCleanupDropTableAfterTabletSplit(CDCCheckpointType checkpoint_type);

  void TestGetTabletListToPollForCDCWithTabletId(CDCCheckpointType checkpoint_type);

  void TestCleanUpCDCStreamsMetadataDuringTabletSplit(CDCCheckpointType checkpoint_type);
};

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTabletSplitDisabledForTablesWithStream)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(/*replication_factor=*/1, /*num_masters=*/1, /*colocated=*/false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  // Should be ok to split before creating a stream.
  ASSERT_OK(XReplValidateSplitCandidateTable(table_id));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  // Split disallowed since FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables is false and we have
  // a CDCSDK stream on the table.
  auto s = XReplValidateSplitCandidateTable(table_id);
  ASSERT_NOK(s);
  ASSERT_NE(
      s.message().AsStringView().find(
          "Tablet splitting is not supported for tables that are a part of a CDCSDK stream"),
      std::string::npos)
      << s.message();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  // Should be ok to split since FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables is true.
  ASSERT_OK(XReplValidateSplitCandidateTable(table_id));
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTabletSplitWithBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;
  // Testing compaction without compaction file filtering for TTL expiration.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;
  // When replica identity is enabled, it takes precedence over what is passed in the command, so we
  // disable it here since we want to use the record type syntax (see PG_FULL below).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = false;

  ASSERT_OK(SetUpWithParams(1, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0.$1 ADD COLUMN $2 INT", "public", kTableName, kValue2ColumnName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);

  xrepl::StreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::EXPLICIT, CDCRecordType::PG_FULL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  LOG(INFO) << "Sleep for 2 seconds to update retention barriers";
  SleepFor(MonoDelta::FromSeconds(2));

  // Insert and Updates.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0($1, $2, $3) VALUES (1, 2, 3)", kTableName, kKeyColumnName, kValueColumnName,
      kValue2ColumnName));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 4 WHERE $2 = 1", kTableName, kValue2ColumnName, kKeyColumnName));
  SleepFor(MonoDelta::FromSeconds(2));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  // GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  // LOG(INFO) << "response = " << change_resp.DebugString();

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_split.size(), 2);

  LOG(INFO) << "Sleep for 2 seconds to update retention barriers";
  SleepFor(MonoDelta::FromSeconds(2));

  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  auto count_after_compaction = CountEntriesInDocDB(peers, table.table_id());

  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;

  // Since inserted only one row and update value column for that row hence after
  // tablet split, only one child tablet will have the row record.
  for (int i = 0; i < tablets_after_split.size(); ++i) {
    GetChangesResponsePB change_resp =
        ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_after_split, nullptr, i));
    LOG(INFO) << "response = " << change_resp.DebugString();
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();

    // This if condition will check if this is the tablet which don't have DML records and
    // only have DDL record.
    if (record_size == 1) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(0);
      ASSERT_EQ(record.row_message().op(), RowMessage::DDL);
      continue;
    }

    int insert_count = 0;
    int update_count = 0;
    for (uint32_t j = 0; j < record_size; ++j) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(j);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        ASSERT_EQ(record.row_message().old_tuple_size(), 3);

        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(1).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(2).datum_int32(), 0);

        ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), 1);
        ASSERT_EQ(record.row_message().new_tuple(1).datum_int32(), 2);
        ASSERT_EQ(record.row_message().new_tuple(2).datum_int32(), 3);
        ASSERT_EQ(record.row_message().table(), kTableName);

        insert_count++;
      } else if (record.row_message().op() == RowMessage::UPDATE) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        ASSERT_EQ(record.row_message().old_tuple_size(), 3);

        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), 1);
        ASSERT_EQ(record.row_message().old_tuple(1).datum_int32(), 2);
        ASSERT_EQ(record.row_message().old_tuple(2).datum_int32(), 3);

        ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), 1);
        if (record.row_message().new_tuple(1).column_name() == kValue2ColumnName) {
          ASSERT_EQ(record.row_message().new_tuple(1).datum_int32(), 4);
          ASSERT_EQ(record.row_message().new_tuple(2).datum_int32(), 2);
        } else {
          ASSERT_EQ(record.row_message().new_tuple(1).datum_int32(), 2);
          ASSERT_EQ(record.row_message().new_tuple(2).datum_int32(), 4);
        }
        ASSERT_EQ(record.row_message().table(), kTableName);
        update_count++;
      }
    }
    ASSERT_EQ(insert_count, 1);
    ASSERT_EQ(update_count, 1);
  }
}

void CDCSDKTabletSplitTest::TestIntentPersistencyAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(WaitForPostApplyMetadataWritten(1 /* expected_num_transactions */));
  ASSERT_OK(FlushTable(table.table_id()));

  int64 initial_num_intents;
  PollForIntentCount(1, 0, IntentCountCompareOption::GreaterThan, &initial_num_intents);
  LOG(INFO) << "Number of intents before tablet split: " << initial_num_intents;

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  LOG(INFO) << "All nodes will be restarted";
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    test_cluster()->mini_tablet_server(i)->Shutdown();
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->Start());
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->WaitStarted());
  }
  LOG(INFO) << "All nodes restarted";

  int64 num_intents_after_restart;
  // Original tablet + two split tablets.
  PollForIntentCount(
      initial_num_intents * 3, 0, IntentCountCompareOption::EqualTo, &num_intents_after_restart);
  LOG(INFO) << "Number of intents after tablet split: " << num_intents_after_restart;
  ASSERT_EQ(num_intents_after_restart, 3 * initial_num_intents);

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint,
      100 /* expected_total_records */, checkpoint_type == CDCCheckpointType::EXPLICIT));
  ASSERT_EQ(received_records, 100);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestIntentPersistencyAfterTabletSplit);

void CDCSDKTabletSplitTest::TestCheckpointPersistencyAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));
  ASSERT_OK(WriteRowsHelper(200, 300, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  SleepFor(MonoDelta::FromSeconds(10));

  OpId cdc_sdk_min_checkpoint = OpId::Invalid();
  for (const auto& peer : test_cluster()->GetTabletPeers(0)) {
    if (peer->tablet_id() == tablets[0].tablet_id()) {
      cdc_sdk_min_checkpoint = peer->cdc_sdk_min_checkpoint_op_id();
      break;
    }
  }
  LOG(INFO) << "Min checkpoint OpId for the tablet peer before tablet split: "
            << cdc_sdk_min_checkpoint;

  ASSERT_OK(SplitTablet(tablets.Get(0).tablet_id(), &test_cluster_));
  SleepFor(MonoDelta::FromSeconds(60));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  LOG(INFO) << "Number of tablets after the split: " << tablets_after_split.size();
  ASSERT_EQ(tablets_after_split.size(), num_tablets * 2);

  for (const auto& peer : test_cluster()->GetTabletPeers(0)) {
    if (peer->tablet_id() == tablets_after_split[0].tablet_id() ||
        peer->tablet_id() == tablets_after_split[1].tablet_id()) {
      LOG(INFO) << "TabletId before split: " << tablets[0].tablet_id();
      ASSERT_LE(peer->cdc_sdk_min_checkpoint_op_id(), cdc_sdk_min_checkpoint);
      LOG(INFO) << "Post split, Tablet: " << peer->tablet_id()
                << ", has the same or lower cdc_sdk_min_checkpoint: " << cdc_sdk_min_checkpoint
                << ", as before tablet split.";
    }
  }
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestCheckpointPersistencyAfterTabletSplit);

void CDCSDKTabletSplitTest::TestTransactionInsertAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  LOG(INFO) << "Tablet split succeded";

  // Now that we have streamed all records from the parent tablet, we expect further calls of
  // 'GetChangesFromCDC' to the same tablet to fail.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto result = GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint());
        if (result.ok() && !result->has_error()) {
          change_resp_1 = *result;
          return false;
        }

        LOG(INFO) << "Encountered error on calling 'GetChanges' on initial parent tablet";
        return true;
      },
      MonoDelta::FromSeconds(90), "GetChanges did not report error for tablet split"));

  ASSERT_OK(WriteRowsHelper(200, 300, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  const int expected_total_records = (checkpoint_type == CDCCheckpointType::EXPLICIT) ? 300 : 100;

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records, expected_total_records);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestTransactionInsertAfterTabletSplit);

void CDCSDKTabletSplitTest::TestGetChangesReportsTabletSplitErrorOnRetries(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  for (int i = 1; i <= 50; i++) {
    ASSERT_OK(WriteRowsHelper(i * 100, (i + 1) * 100, &test_cluster_, true));
  }
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));

  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  // Get the OpId of the last latest successful operation.
  tablet::RemoveIntentsData data;
  for (const auto& peer : test_cluster()->GetTabletPeers(0)) {
    if (peer->tablet_id() == tablets[0].tablet_id()) {
      ASSERT_OK(peer->tablet()->transaction_participant()->context()->GetLastReplicatedData(&data));
    }
  }

  // Create a CDCSDK checkpoint term with the OpId of the last successful operation.
  CDCSDKCheckpointPB new_checkpoint;
  new_checkpoint.set_term(data.op_id.term);
  new_checkpoint.set_index(data.op_id.index);

  // Initiate a tablet split request, since there are around 5000 rows in the table/ tablet, it will
  // take some time for the child tablets to be in running state.
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  // Keep calling 'GetChange' until we get an error for the tablet split, this will only happen
  // after both the child tablets are in running state.
  GetChangesResponsePB change_resp;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto result = GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint());
        if (result.ok() && !result->has_error()) {
          change_resp = *result;
          return false;
        }

        LOG(INFO) << "Encountered error on calling 'GetChanges' on initial parent tablet";
        return true;
      },
      MonoDelta::FromSeconds(90), "GetChanges did not report error for tablet split"));
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetChangesReportsTabletSplitErrorOnRetries);

void CDCSDKTabletSplitTest::TestGetChangesAfterTabletSplitWithMasterShutdown(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  test_cluster_.mini_cluster_->mini_master()->Shutdown();
  ASSERT_OK(test_cluster_.mini_cluster_->StartMasters());
  LOG(INFO) << "Restart master before tablet split succesfull";
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  SleepFor(MonoDelta::FromSeconds(5));
  test_cluster_.mini_cluster_->mini_master()->Shutdown();
  ASSERT_OK(test_cluster_.mini_cluster_->StartMasters());
  LOG(INFO) << "Restart master after tablet split succesfull";

  // We must still be able to get the remaining records from the parent tablet even after master is
  // restarted.

  // Since split is complete at this stage, parent tablet will report an error
  // and we should be able to get the records from the children.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  const int expected_total_records = 200;

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records, expected_total_records);
  LOG(INFO) << "Number of records after restart: " << received_records;
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetChangesAfterTabletSplitWithMasterShutdown);

void CDCSDKTabletSplitTest::TestGetChangesOnChildrenOnSplit(CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split,
                                      /* partition_list_version =*/nullptr));

  const int64 safe_hybrid_time = change_resp_1.safe_hybrid_time();
  const int wal_segment_index = change_resp_1.wal_segment_index();

  // Calling GetChanges on both the children to ensure that the test doesn't fail with
  // the invalid checkpoint error.
  GetChangesResponsePB child_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_after_split, nullptr, 0,
                                      safe_hybrid_time, wal_segment_index, false));

  GetChangesResponsePB child_resp_2 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_after_split, nullptr, 1,
                                      safe_hybrid_time, wal_segment_index, false));
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetChangesOnChildrenOnSplit);

void CDCSDKTabletSplitTest::TestGetChangesOnParentTabletAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  LOG(INFO) << "All nodes will be restarted";
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    test_cluster()->mini_tablet_server(i)->Shutdown();
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->Start());
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->WaitStarted());
  }
  LOG(INFO) << "All nodes restarted";
  SleepFor(MonoDelta::FromSeconds(10));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));

  const int expected_total_records = 200;

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records, expected_total_records);
  LOG(INFO) << "Number of records after restart: " << received_records;
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetChangesOnParentTabletAfterTabletSplit);

void CDCSDKTabletSplitTest::TestGetChangesMultipleStreamsTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id_1 = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  xrepl::StreamId stream_id_2 = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_1, tablets));
  ASSERT_FALSE(resp.has_error());
  resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_2, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets));
  GetChangesResponsePB change_resp_2 = ASSERT_RESULT(GetChangesFromCDC(stream_id_2, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(WriteRowsHelper(0, 100, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));

  // Call GetChanges only on one stream so that the other stream will be lagging behind.
  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));

  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_NOK(GetChangesFromCDC(stream_id_1, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  const int expected_total_records_1 =
    (checkpoint_type == CDCCheckpointType::EXPLICIT) ? 200 : 100;

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint_1;
  tablet_to_checkpoint_1[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();
  int64 received_records_1 = ASSERT_RESULT(GetChangeRecordCount(
      stream_id_1, table, tablets, tablet_to_checkpoint_1, expected_total_records_1,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records_1, expected_total_records_1);

  LOG(INFO) << "Number of records on first stream after split: " << received_records_1;

  const int expected_total_records_2 = 200;
  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint_2;
  tablet_to_checkpoint_2[tablets.Get(0).tablet_id()] = change_resp_2.cdc_sdk_checkpoint();
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));
  int64 received_records_2 = ASSERT_RESULT(GetChangeRecordCount(
      stream_id_2, table, tablets, tablet_to_checkpoint_2, expected_total_records_2,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records_2, expected_total_records_2);

  LOG(INFO) << "Number of records on second stream after split: " << received_records_2;
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetChangesMultipleStreamsTabletSplit);

void CDCSDKTabletSplitTest::TestSetCDCCheckpointAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_before_split;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_before_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_before_split.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));

  ASSERT_OK(WriteRowsHelper(0, 1000, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));
  WaitUntilSplitIsSuccesful(tablets_before_split.Get(0).tablet_id(), table);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_split.size(), 2);

  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets_after_split, OpId::Min(), true, 0));
  ASSERT_FALSE(resp.has_error());

  resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets_after_split, OpId::Min(), true, 1));
  ASSERT_FALSE(resp.has_error());
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestSetCDCCheckpointAfterTabletSplit);

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTabletSplitBeforeBootstrap)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_metrics_interval_ms) = 5000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  uint32_t num_tservers = 3;
  ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  ASSERT_RESULT(CreateDBStream(IMPLICIT));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  SleepFor(MonoDelta::FromSeconds(10));

  // We are checking the 'cdc_state' table just after tablet split is succesfull, but since we
  // haven't started streaming from the parent tablet, we should only see 2 rows.
  uint seen_rows = 0;
  TabletId parent_tablet_id = tablets[0].tablet_id();
  CDCStateTable cdc_state_table(test_client());
  Status s;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        for (auto row_result : VERIFY_RESULT(cdc_state_table.GetTableRange(
                 CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
          RETURN_NOT_OK(row_result);
          auto& row = *row_result;
          const auto& checkpoint = *row.checkpoint;
          LOG(INFO) << "Read cdc_state table row for tablet_id: " << row.key.tablet_id
                    << " and stream_id: " << row.key.stream_id
                    << ", with checkpoint: " << checkpoint;

          if (row.key.tablet_id != tablets[0].tablet_id()) {
            // Both children should have the min OpId(-1.-1) as the checkpoint.
            ++seen_rows;
          }
        }

        return seen_rows == 2;
      },
      MonoDelta::FromSeconds(60), "Waiting for verifying children tablets' checkpoint"));

  ASSERT_OK(s);
  ASSERT_EQ(seen_rows, 2);

  // Since we haven't started polling yet, the checkpoint in the tablet peers would be OpId(-1.-1).
  for (uint tserver_index = 0; tserver_index < num_tservers; tserver_index++) {
    for (const auto& peer : test_cluster()->GetTabletPeers(tserver_index)) {
      if (peer->tablet_id() == tablets[0].tablet_id()) {
        ASSERT_EQ(OpId::Invalid(), peer->cdc_sdk_min_checkpoint_op_id());
      }
    }
  }
}

void CDCSDKTabletSplitTest::TestCDCStateTableAfterTabletSplit(CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_metrics_interval_ms) = 5000;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  SleepFor(MonoDelta::FromSeconds(10));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));

  // We are checking the 'cdc_state' table just after tablet split is succesfull, so we must see 3
  // entries, one for the parent tablet and two for the children tablets.
  uint seen_rows = 0;
  TabletId parent_tablet_id = tablets[0].tablet_id();
  CDCStateTable cdc_state_table(test_client());
  Status s;
  for (auto row_result : ASSERT_RESULT(
           cdc_state_table.GetTableRange(CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    auto& checkpoint = *row.checkpoint;
    LOG(INFO) << "Read cdc_state table row for tablet_id: " << row.key.tablet_id
              << " and stream_id: " << row.key.stream_id << ", with checkpoint: " << checkpoint;

    if (row.key.tablet_id != tablets[0].tablet_id()) {
      // Both children should have the min OpId(0.0) as the checkpoint.
      ASSERT_EQ(checkpoint, OpId::Min());
    }

    seen_rows += 1;
  }
  ASSERT_OK(s);

  ASSERT_EQ(seen_rows, 3);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestCDCStateTableAfterTabletSplit);

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestCDCStateTableAfterTabletSplitReported)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(EXPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  const int expected_total_records = 200;

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records, true));
  ASSERT_EQ(received_records, expected_total_records);
  LOG(INFO) << "Number of records after restart: " << received_records;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(20));

  bool saw_row_child_one = false;
  bool saw_row_child_two = false;
  bool saw_row_parent = false;

  // We will not be seeing the entry corresponding to the parent tablet since that is deleted now.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  CDCStateTable cdc_state_table(test_client());
  Status s;
  for (auto row_result : ASSERT_RESULT(
           cdc_state_table.GetTableRange({} /* just key columns */, &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    LOG(INFO) << "Read cdc_state table row with tablet_id: " << row.key.tablet_id
              << " stream_id: " << row.key.stream_id;

    if (row.key.tablet_id == tablets_after_split[0].tablet_id()) {
      saw_row_child_one = true;
    } else if (row.key.tablet_id == tablets_after_split[1].tablet_id()) {
      saw_row_child_two = true;
    } else if (row.key.tablet_id == parent_tablet_id) {
      saw_row_parent = true;
    }
  }
  ASSERT_OK(s);

  ASSERT_TRUE(saw_row_child_one);
  ASSERT_TRUE(saw_row_child_two);
  ASSERT_FALSE(saw_row_parent);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> first_tablet_after_split;
  first_tablet_after_split.CopyFrom(tablets_after_split);
  ASSERT_EQ(first_tablet_after_split[0].tablet_id(), tablets_after_split[0].tablet_id());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> second_tablet_after_split;
  second_tablet_after_split.CopyFrom(tablets_after_split);
  second_tablet_after_split.DeleteSubrange(0, 1);
  ASSERT_EQ(second_tablet_after_split.size(), 1);
  ASSERT_EQ(second_tablet_after_split[0].tablet_id(), tablets_after_split[1].tablet_id());
}

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCAfterTabletSplitReported(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  // Call on parent tablet should fail since that has been split.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "The tablet split error is now communicated to the client.";

  const int expected_total_records = 200;

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records, expected_total_records);
  LOG(INFO) << "Number of records after restart: " << received_records;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_split, nullptr));

  auto get_tablets_resp =
      ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id, tablets[0].tablet_id()));
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 2);

  // Assert that GetTabletListToPollForCDC also populates the snapshot_time.
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    ASSERT_GE(tablet_checkpoint_pair.cdc_sdk_checkpoint().snapshot_time(), 0);
  }

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(2));

  bool saw_row_child_one = false;
  bool saw_row_child_two = false;
  // We should no longer see the entry corresponding to the parent tablet.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_TRUE(parent_tablet_id != tablet_id);

    if (tablet_id == tablets_after_split[0].tablet_id()) {
      saw_row_child_one = true;
    } else if (tablet_id == tablets_after_split[1].tablet_id()) {
      saw_row_child_two = true;
    }
  }

  ASSERT_TRUE(saw_row_child_one && saw_row_child_two);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetTabletListToPollForCDCAfterTabletSplitReported);

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCBeforeTabletSplitReported(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  // We are calling: "GetTabletListToPollForCDC" when the client has not yet streamed all the data
  // from the parent tablet.
  auto get_tablets_resp = ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id));

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(2));

  // We should only see the entry corresponding to the parent tablet.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 1);
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_TRUE(parent_tablet_id == tablet_id);
  }
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
    TestGetTabletListToPollForCDCBeforeTabletSplitReported);

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCBootstrapWithTabletSplit(
    CDCCheckpointType checkpoint_type) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  // We are calling: "GetTabletListToPollForCDC" when the client has not yet started streaming.
  auto get_tablets_resp = ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));

  bool saw_row_child_one = false;
  bool saw_row_child_two = false;
  // We should only see the entry corresponding to the children tablets.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_TRUE(parent_tablet_id != tablet_id);

    if (tablet_id == tablets_after_split[0].tablet_id()) {
      saw_row_child_one = true;
    } else if (tablet_id == tablets_after_split[1].tablet_id()) {
      saw_row_child_two = true;
    }
  }

  ASSERT_TRUE(saw_row_child_one && saw_row_child_two);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetTabletListToPollForCDCBootstrapWithTabletSplit);

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCBootstrapWithTwoTabletSplits(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  LOG(INFO) << "First tablet split succeded on tablet: " << tablets[0].tablet_id();

  ASSERT_OK(WriteRowsHelper(200, 400, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_first_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  // Now we again split one of the child tablets.
  WaitUntilSplitIsSuccesful(
      tablets_after_first_split.Get(0).tablet_id(), table, 3 /*expected_num_tablets*/);
  LOG(INFO) << "Second tablet split succeded on tablet: "
            << tablets_after_first_split[0].tablet_id();

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_second_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_second_split, /* partition_list_version =*/nullptr));

  // We are calling: "GetTabletListToPollForCDC" when the client has not yet started streaming.
  auto get_tablets_resp = ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id));

  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs_size(), 3);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
    TestGetTabletListToPollForCDCBootstrapWithTwoTabletSplits);

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCWithTwoTabletSplits(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRowsHelper(200, 400, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_first_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(0).tablet_id(), table, 3);

  // GetChanges call would fail since the tablet is split.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  // We are calling: "GetTabletListToPollForCDC" when the tablet split on the parent tablet has
  // still not been communicated to the client. Hence we should get only the original parent tablet.
  auto get_tablets_resp = ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id));
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 1);
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_EQ(tablet_id, tablets[0].tablet_id());
  }

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(2));

  // We are calling: "GetTabletListToPollForCDC" when the client has streamed all the data from the
  // parent tablet.
  get_tablets_resp =
      ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id, tablets[0].tablet_id()));

  // We should only see the entries for the 2 child tablets, which were created after the first
  // tablet split.
  bool saw_first_child = false;
  bool saw_second_child = false;
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 2);
  const auto& parent_tablet_id = tablets[0].tablet_id();
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_TRUE(parent_tablet_id != tablet_id);

    if (tablet_id == tablets_after_first_split[0].tablet_id()) {
      saw_first_child = true;
    } else if (tablet_id == tablets_after_first_split[1].tablet_id()) {
      saw_second_child = true;
    }
  }
  ASSERT_TRUE(saw_first_child && saw_second_child);

  // These calls will return errors since the tablet have been split further.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets_after_first_split,
                               &change_resp_1.cdc_sdk_checkpoint(), 0));


  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();

  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, 400,
      checkpoint_type == CDCCheckpointType::EXPLICIT));
  ASSERT_EQ(received_records, 400);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetTabletListToPollForCDCWithTwoTabletSplits);

void CDCSDKTabletSplitTest::TestTabletSplitOnAddedTableForCDC(CDCCheckpointType checkpoint_type) {
  ASSERT_OK(SetUpWithParams(1, 1, false));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  std::vector<TableId> expected_table_ids;
  expected_table_ids.reserve(2);
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  expected_table_ids.push_back(table_id);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));

  std::unordered_set<TabletId> expected_tablet_ids;
  for (const auto& tablet : tablets) {
    expected_tablet_ids.insert(tablet.tablet_id());
  }

  auto table_2 =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table_1", num_tablets));
  TableId table_2_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, "test_table_1"));
  expected_table_ids.push_back(table_2_id);
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_2;
  ASSERT_OK(
      test_client()->GetTablets(table_2, 0, &tablets_2, /* partition_list_version =*/nullptr));
  for (const auto& tablet : tablets_2) {
    expected_tablet_ids.insert(tablet.tablet_id());
  }
  ASSERT_EQ(expected_tablet_ids.size(), num_tablets * 2);

  // Verify that table_2's tablets have been added to the cdc_state table.
  CheckTabletsInCDCStateTable(expected_tablet_ids, test_client());
  SleepFor(MonoDelta::FromSeconds(1));

  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets_2));
  ASSERT_FALSE(resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_2));
  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_2, &change_resp.cdc_sdk_checkpoint()));

  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true, 2, "test_table_1"));
  ASSERT_OK(WaitForFlushTables(
      {table_2_id}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  WaitUntilSplitIsSuccesful(tablets_2.Get(0).tablet_id(), table_2);

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table_2, tablets_2, tablet_to_checkpoint, 200,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records, 200);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestTabletSplitOnAddedTableForCDC);

void CDCSDKTabletSplitTest::TestTabletSplitOnAddedTableForCDCWithMasterRestart(
    CDCCheckpointType checkpoint_type) {
  ASSERT_OK(SetUpWithParams(1, 1, false));

  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  std::vector<TableId> expected_table_ids;
  expected_table_ids.reserve(2);
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  expected_table_ids.push_back(table_id);
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));

  std::unordered_set<TabletId> expected_tablet_ids;
  for (const auto& tablet : tablets) {
    expected_tablet_ids.insert(tablet.tablet_id());
  }

  auto table_2 =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "test_table_1", num_tablets));
  TableId table_2_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, "test_table_1"));
  expected_table_ids.push_back(table_2_id);
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_2;
  ASSERT_OK(
      test_client()->GetTablets(table_2, 0, &tablets_2, /* partition_list_version =*/nullptr));
  for (const auto& tablet : tablets_2) {
    expected_tablet_ids.insert(tablet.tablet_id());
  }
  ASSERT_EQ(expected_tablet_ids.size(), num_tablets * 2);

  // Verify that table_2's tablets have been added to the cdc_state table.
  CheckTabletsInCDCStateTable(expected_tablet_ids, test_client());

  test_cluster_.mini_cluster_->mini_master()->Shutdown();
  ASSERT_OK(test_cluster_.mini_cluster_->StartMasters());
  LOG(INFO) << "Restarted Master";
  SleepFor(MonoDelta::FromSeconds(30));

  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets_2));
  ASSERT_FALSE(resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_2));
  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_2, &change_resp.cdc_sdk_checkpoint()));

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true, 2, "test_table_1"));
  ASSERT_OK(WaitForFlushTables(
      {table_2_id}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  WaitUntilSplitIsSuccesful(tablets_2.Get(0).tablet_id(), table_2);

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets_2.Get(0).tablet_id()] = change_resp.cdc_sdk_checkpoint();
  int64 record_count = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table_2, tablets_2, tablet_to_checkpoint, 199,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  // Verify the count of records for the table.
  ASSERT_EQ(record_count, 199);
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTabletSplitDuringSnapshot)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto set_resp = ASSERT_RESULT(
      SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_tablet_split = true;
  while (true) {
    GetChangesResponsePB change_resp_updated =
        ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();

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

    if (do_tablet_split) {
      ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
      WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
      LOG(INFO) << "Tablet split succeded";
      do_tablet_split = false;
    }

    // End of the snapshot records.
    if (change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
  }
  ASSERT_EQ(reads_snapshot, 200);
}

void CDCSDKTabletSplitTest::TestTransactionCommitAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;

  uint32_t num_columns = 10;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, kTableName, num_tablets, true, false, 0, false, "", "public",
      num_columns));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  // Initiate a transaction.
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN"));

  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  // Insert 50 rows as part of the initiated transaction.
  for (uint32_t i = 200; i < 400; ++i) {
    uint32_t value = i;
    std::stringstream statement_buff;
    statement_buff << "INSERT INTO $0 VALUES (";
    for (uint32_t iter = 0; iter < num_columns; ++value, ++iter) {
      statement_buff << value << ",";
    }

    std::string statement(statement_buff.str());
    statement.at(statement.size() - 1) = ')';
    ASSERT_OK(conn.ExecuteFormat(statement, kTableName));
  }

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  LOG(INFO) << "Tablet split succeeded";

  // Commit the trasaction after the tablet split.
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_split.size(), num_tablets * 2);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> first_tablet_after_split;
  first_tablet_after_split.CopyFrom(tablets_after_split);
  ASSERT_EQ(first_tablet_after_split[0].tablet_id(), tablets_after_split[0].tablet_id());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> second_tablet_after_split;
  second_tablet_after_split.CopyFrom(tablets_after_split);
  second_tablet_after_split.DeleteSubrange(0, 1);
  ASSERT_EQ(second_tablet_after_split.size(), 1);
  ASSERT_EQ(second_tablet_after_split[0].tablet_id(), tablets_after_split[1].tablet_id());

  while (true) {
    auto result = GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint());
    if (!result.ok() || result->has_error()) {
      // We break out of the loop when 'GetChanges' reports an error.
      break;
    }
    change_resp_1 = *result;
  }

  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = change_resp_1.cdc_sdk_checkpoint();

  int64 received_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, 400,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  ASSERT_EQ(received_records, 400);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestTransactionCommitAfterTabletSplit);

void CDCSDKTabletSplitTest::TestTabletSplitBeforeBootstrapGetCheckpoint(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_metrics_interval_ms) = 5000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1000;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  uint32_t num_tservers = 3;
  ASSERT_OK(SetUpWithParams(num_tservers, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));

  auto checkpoints = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets_after_split));
  for (const auto& checkpoint : checkpoints) {
    ASSERT_FALSE(checkpoint.valid());
  }
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestTabletSplitBeforeBootstrapGetCheckpoint);

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCWithOnlyOnePolledChild(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  while (true) {
    auto result = GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint());
    if (!result.ok() || result->has_error()) {
      // We break out of the loop when 'GetChanges' reports an error.
      break;
    }
    change_resp_1 = *result;
  }

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> first_tablet_after_split;
  first_tablet_after_split.CopyFrom(tablets_after_split);
  ASSERT_EQ(first_tablet_after_split[0].tablet_id(), tablets_after_split[0].tablet_id());

  // Only call GetChanges on one of the children.
  ASSERT_RESULT(
      GetChangesFromCDC(stream_id, first_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint()));

  // We are calling: "GetTabletListToPollForCDC" when the client
  auto get_tablets_resp = ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id));

  // We should only see the entry corresponding to the parent tablet.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 1);
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_TRUE(parent_tablet_id == tablet_id);
  }
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetTabletListToPollForCDCWithOnlyOnePolledChild);

void CDCSDKTabletSplitTest::TestRecordCountsAfterMultipleTabletSplits(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRows(0, 200, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRows(200, 400, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(0).tablet_id(), table, 3);
  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(1).tablet_id(), table, 4);

  ASSERT_OK(WriteRows(400, 600, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_third_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_third_split, nullptr));
  ASSERT_EQ(tablets_after_third_split.size(), 4);

  WaitUntilSplitIsSuccesful(tablets_after_third_split.Get(1).tablet_id(), table, 5);

  ASSERT_OK(WriteRows(600, 1000, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  const int expected_total_records = 1000;
  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  int64 total_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  LOG(INFO) << "Got " << total_records << " records";
  ASSERT_EQ(expected_total_records, total_records);
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestSplitAfterSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRows(0, 200, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRows(200, 400, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(0).tablet_id(), table, 3);
  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(1).tablet_id(), table, 4);

  // Don't insert anything, just split the tablets further
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_third_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_third_split, nullptr));
  ASSERT_EQ(tablets_after_third_split.size(), 4);

  WaitUntilSplitIsSuccesful(tablets_after_third_split.Get(1).tablet_id(), table, 5);

  std::map<TabletId, CDCSDKCheckpointPB> checkpoint_map;
  int64 received_records =
      ASSERT_RESULT(GetChangeRecordCount(stream_id, table, tablets, checkpoint_map, 400));

  ASSERT_EQ(received_records, 400);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> final_tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &final_tablets, nullptr));

  std::unordered_set<TabletId> expected_tablet_ids;
  for (int i = 0; i < final_tablets.size(); ++i) {
    expected_tablet_ids.insert(final_tablets.Get(i).tablet_id());
  }

  // Verify that the cdc_state has only current set of children tablets.
  CDCStateTable cdc_state_table(test_client());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s;
        std::unordered_set<TabletId> tablets_found;
        for (auto row_result : VERIFY_RESULT(cdc_state_table.GetTableRange(
                 CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
          RETURN_NOT_OK(row_result);
          auto& row = *row_result;
          if (row.key.stream_id == stream_id && !expected_tablet_ids.contains(row.key.tablet_id)) {
            // Still have a tablet left over from a dropped table.
            return false;
          }
          if (row.key.stream_id == stream_id) {
            tablets_found.insert(row.key.tablet_id);
          }
        }
        RETURN_NOT_OK(s);
        LOG(INFO) << "tablets found: " << AsString(tablets_found)
                  << ", expected tablets: " << AsString(expected_tablet_ids);
        return expected_tablet_ids == tablets_found;
      },
      MonoDelta::FromSeconds(60), "Waiting for stream metadata cleanup."));
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestRecordCountsAfterMultipleTabletSplits);

void CDCSDKTabletSplitTest::TestRecordCountAfterMultipleTabletSplitsInMultiNodeCluster(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRows(0, 200, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRows(200, 400, &test_cluster_));
  ASSERT_OK(WaitForFlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  const int expected_total_records = 400;
  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  int64 total_records = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, expected_total_records,
      checkpoint_type == CDCCheckpointType::EXPLICIT));

  LOG(INFO) << "Got " << total_records << " records";
  ASSERT_EQ(expected_total_records, total_records);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestRecordCountAfterMultipleTabletSplitsInMultiNodeCluster);

// Ensures that a drop table (table 2) after a tablet split (table 1) from the same namespace
// doesn't lead to the deletion of parent tablet entry for table 1 in the cdc_state table.
void CDCSDKTabletSplitTest::TestStreamMetaDataCleanupDropTableAfterTabletSplit(
    CDCCheckpointType checkpoint_type) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const vector<string> table_list_suffix = {"_1", "_2", "_3"};
  const int kNumTables = 3;
  vector<YBTableName> table(kNumTables);
  vector<TableId> table_ids(kNumTables);
  int idx = 0;
  vector<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> tablets(kNumTables);
  vector<std::string> tablet_ids_before_split;
  vector<std::string> tablet_ids_before_drop;

  for (auto table_suffix : table_list_suffix) {
    table[idx] = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, true, table_suffix));
    ASSERT_OK(test_client()->GetTablets(
        table[idx], 0, &tablets[idx], /* partition_list_version = */ nullptr));

    ASSERT_OK(WriteEnumsRows(
        0 /* start */, 100 /* end */, &test_cluster_, table_suffix, kNamespaceName, kTableName));
    table_ids[idx] = table[idx].table_id();
    idx += 1;
  }
  auto stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));

  // Consume changes for table 1 whose tablet will be split later. It ensures that the parent tablet
  // remains in the cdc_state table after the split is complete.
  // If we don't consume the changes here, upon the tablet split, the parent tablet id will be
  // removed from the cdc_state table as an optimization which we don't want here.
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets[0]));
  ASSERT_FALSE(resp.has_error());
  auto change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets[0]));
  ASSERT_OK(WriteEnumsRows(
      100 /* start */, 200 /* end */, &test_cluster_, table_list_suffix[0], kNamespaceName,
      kTableName));

  ASSERT_OK(WaitForFlushTables(
      table_ids, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  // Split tablet of table 1.
  WaitUntilSplitIsSuccesful(tablets[0].Get(0).tablet_id(), table[0]);
  LOG(INFO) << "Tablet split succeded";

  // Drop table3 from the namespace.
  DropTable(&test_cluster_, "test_table_3");
  LOG(INFO) << "Dropped test_table_3 from namespace";

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        while (true) {
          auto get_resp = GetDBStreamInfo(stream_id);
          // Wait until the background thread cleanup up the drop table metadata.
          if (get_resp.ok() && !get_resp->has_error() && get_resp->table_info_size() == 2) {
            return true;
          }
        }
      },
      MonoDelta::FromSeconds(60), "Waiting for stream metadata cleanup."));

  // Verify that cdc_state has tablets from table1 (both parent and child tablets) and table2 left.
  std::unordered_set<TabletId> expected_tablet_ids;
  expected_tablet_ids.insert(tablets[0].Get(0).tablet_id()); // parent tablet
  for (const auto& tablet : tablets[1]) {
    expected_tablet_ids.insert(tablet.tablet_id());
  }
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table1_tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table[0], 0, &table1_tablets_after_split, /* partition_list_version =*/nullptr));
  for (const auto& tablet : table1_tablets_after_split) {
    expected_tablet_ids.insert(tablet.tablet_id());
  }

  CDCStateTable cdc_state_table(test_client());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s;
        std::unordered_set<TabletId> tablets_found;
        for (auto row_result : VERIFY_RESULT(cdc_state_table.GetTableRange(
                 CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
          RETURN_NOT_OK(row_result);
          auto& row = *row_result;
          if (row.key.stream_id == stream_id && !expected_tablet_ids.contains(row.key.tablet_id)) {
            // Still have a tablet left over from a dropped table.
            return false;
          }
          if (row.key.stream_id == stream_id) {
            tablets_found.insert(row.key.tablet_id);
          }
        }
        RETURN_NOT_OK(s);
        LOG(INFO) << "tablets found: " << AsString(tablets_found)
                  << ", expected tablets: " << AsString(expected_tablet_ids);
        return expected_tablet_ids == tablets_found;
      },
      MonoDelta::FromSeconds(60), "Waiting for stream metadata cleanup."));

  // Deleting the created stream.
  ASSERT_TRUE(DeleteCDCStream(stream_id));
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestStreamMetaDataCleanupDropTableAfterTabletSplit);

void CDCSDKTabletSplitTest::TestGetTabletListToPollForCDCWithTabletId(
    CDCCheckpointType checkpoint_type) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(0, 200, &test_cluster_, true));
  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  auto cp = change_resp_1.cdc_sdk_checkpoint();
  std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint;
  tablet_to_checkpoint[tablets.Get(0).tablet_id()] = cp;

  int64 record_count = ASSERT_RESULT(GetChangeRecordCount(
      stream_id, table, tablets, tablet_to_checkpoint, 200,
      checkpoint_type == CDCCheckpointType::EXPLICIT));
  ASSERT_EQ(record_count, 200);

  auto get_tablets_resp =
      ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id, tablets[0].tablet_id()));
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 2);

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(2));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));

  bool saw_row_child_one = false;
  bool saw_row_child_two = false;
  // We should no longer see the entry corresponding to the parent tablet.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_TRUE(parent_tablet_id != tablet_id);

    if (tablet_id == tablets_after_split[0].tablet_id()) {
      saw_row_child_one = true;
    } else if (tablet_id == tablets_after_split[1].tablet_id()) {
      saw_row_child_two = true;
    }
  }

  ASSERT_TRUE(saw_row_child_one && saw_row_child_two);
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestGetTabletListToPollForCDCWithTabletId);

void CDCSDKTabletSplitTest::TestCleanUpCDCStreamsMetadataDuringTabletSplit(
    CDCCheckpointType checkpoint_type) {
  SyncPoint::GetInstance()->LoadDependency(
      {{"Test::AfterTableDrop", "CleanUpCDCStreamMetadata::StartStep1"},
      {"CleanUpCDCStreamMetadata::CompletedStep1", "Test::InitiateTabletSplit"},
      {"Test::AfterTabletSplit", "CleanUpCDCStreamMetadata::StartStep2"}});
  SyncPoint::GetInstance()->EnableProcessing();
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(/* replication_factor */ 1, /* num_master */ 1, /* colocated */ false));
  const vector<string> table_list_suffix = {"_1", "_2"};
  const int kNumTables = 2;
  vector<YBTableName> table(kNumTables);
  vector<TableId> table_ids(kNumTables);
  int idx = 0;
  vector<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> tablets(kNumTables);

  for (auto table_suffix : table_list_suffix) {
    table[idx] = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, true, table_suffix));
    ASSERT_OK(test_client()->GetTablets(
        table[idx], 0, &tablets[idx], /* partition_list_version = */ nullptr));
    ASSERT_OK(WriteEnumsRows(
        0 /* start */, 100 /* end */, &test_cluster_, table_suffix, kNamespaceName, kTableName));
    table_ids[idx] = table[idx].table_id();
    idx += 1;
  }
  auto stream_id = ASSERT_RESULT(CreateDBStreamBasedOnCheckpointType(checkpoint_type));

  // Consume changes for table 1 whose tablet will be split later. It ensures that the parent tablet
  // remains in the cdc_state table after the split is complete.
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets[0]));
  ASSERT_FALSE(resp.has_error());
  ASSERT_OK(WaitForFlushTables(
      table_ids, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  // Drop table2 from the namespace.
  DropTable(&test_cluster_, "test_table_2");
  LOG(INFO) << "Dropped test_table_2 from namespace";
  TEST_SYNC_POINT("Test::AfterTableDrop");

  // Wait for cleanup bg thread to complete step-1.
  TEST_SYNC_POINT("Test::InitiateTabletSplit");
  // Split tablet of table 1.
  WaitUntilSplitIsSuccesful(tablets[0].Get(0).tablet_id(), table[0]);
  LOG(INFO) << "Tablet split succeded";
  TEST_SYNC_POINT("Test::AfterTabletSplit");

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

  // Verify that cdc_state table contains 3 entries i.e. parent tablet + 2 children tablets
  // of table1.
  std::unordered_set<TabletId> expected_tablet_ids;
  expected_tablet_ids.insert(tablets[0].Get(0).tablet_id()); // parent tablet of table1

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> table1_tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table[0], 0, &table1_tablets_after_split, /* partition_list_version =*/nullptr));
  for (const auto& tablet : table1_tablets_after_split) {
    expected_tablet_ids.insert(tablet.tablet_id()); // Children tablets of table1
  }

  // Incase there is some lag in completing the execution of delete operation on cdc_state table
  // triggered by the CleanUpCDCStreamMetadata thread.
  SleepFor(MonoDelta::FromSeconds(2));
  CDCStateTable cdc_state_table(test_client());
  Status s;
  std::unordered_set<TabletId> tablets_found;
  for (auto row_result : ASSERT_RESULT(cdc_state_table.GetTableRange(
            CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    LOG(INFO) << "Read cdc_state table row with tablet_id: " << row.key.tablet_id << " stream_id: "
              << row.key.stream_id << " checkpoint: " << row.checkpoint->ToString();
    if (row.key.stream_id == stream_id) {
      tablets_found.insert(row.key.tablet_id);
    }
  }
  ASSERT_OK(s);
  LOG(INFO) << "tablets found: " << AsString(tablets_found)
            << ", expected tablets: " << AsString(expected_tablet_ids);
  ASSERT_EQ(expected_tablet_ids, tablets_found);

  ASSERT_TRUE(DeleteCDCStream(stream_id));
}

CDCSDK_TESTS_FOR_ALL_CHECKPOINT_OPTIONS(CDCSDKTabletSplitTest,
                                        TestCleanUpCDCStreamsMetadataDuringTabletSplit);

TEST_F(CDCSDKTabletSplitTest, TestTabletSplitDuringConsistentSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  // Table having key:value_1 column
  ASSERT_OK(WriteRows(1 /* start */, 201 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  // GetCheckpoint after snapshot bootstrap (done as part of stream creation itself).
  auto cp_resp = ASSERT_RESULT(GetCDCSDKSnapshotCheckpoint(stream_id, tablets[0].tablet_id()));

  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool first_read = true;
  bool do_tablet_split = true;
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

    if (do_tablet_split) {
      // ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
      ASSERT_OK(WaitForFlushTables(
          {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
          /* is_compaction = */ false));
      WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
      LOG(INFO) << "Tablet split succeded";
      do_tablet_split = false;
    }

    // End of the snapshot records.
    if (change_resp_updated.cdc_sdk_checkpoint().write_id() == 0 &&
        change_resp_updated.cdc_sdk_checkpoint().snapshot_time() == 0) {
      break;
    }
  }
  ASSERT_EQ(reads_snapshot, 200);
}

TEST_F(CDCSDKTabletSplitTest, TestTabletSplitAfterConsistentSnapshotStreamCreation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_snapshot_batch_size) = 100;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, kNamespaceName, kTableName));
  // Table having key:value_1 column
  ASSERT_OK(WriteRows(1 /* start */, 10 /* end */, &test_cluster_));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateConsistentSnapshotStream());

  ASSERT_OK(WaitForFlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  LOG(INFO) << "Tablet split succeded";

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, nullptr, RequireTabletsRunning::kFalse,
      master::IncludeInactive::kTrue));
  // tablets_after_split should have 3 tablets - one parent & two childrens
  ASSERT_EQ(tablets_after_split.size(), 3);

  ASSERT_OK(WriteRows(10 /* start */, 15 /* end */, &test_cluster_));

  auto get_tablets_resp = ASSERT_RESULT(
                GetTabletListToPollForCDC(stream_id, table.table_id()));
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs_size(), 1);
  auto tablet_id = get_tablets_resp.tablet_checkpoint_pairs()[0].tablet_locations().tablet_id();
  // tablet_id received in getTabletListToPoll should be parent's tablet_id.
  ASSERT_EQ(tablet_id, tablets[0].tablet_id());
}

}  // namespace cdc
}  // namespace yb
