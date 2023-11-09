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

#include "yb/cdc/cdc_state_table.h"

namespace yb {
namespace cdc {

class CDCSDKTabletSplitTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  }
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
  ASSERT_OK(XreplValidateSplitCandidateTable(table_id));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  // Split disallowed since FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables is false and we have
  // a CDCSDK stream on the table.
  auto s = XreplValidateSplitCandidateTable(table_id);
  ASSERT_NOK(s);
  ASSERT_NE(
      s.message().AsStringView().find(
          "Tablet splitting is not supported for tables that are a part of a CDCSDK stream"),
      std::string::npos)
      << s.message();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables) = true;
  // Should be ok to split since FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables is true.
  ASSERT_OK(XreplValidateSplitCandidateTable(table_id));
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestIntentPersistencyAfterTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  int64 initial_num_intents;
  PollForIntentCount(1, 0, IntentCountCompareOption::GreaterThan, &initial_num_intents);
  LOG(INFO) << "Number of intents before tablet split: " << initial_num_intents;

  ASSERT_OK(SplitTablet(tablets.Get(0).tablet_id(), &test_cluster_));

  LOG(INFO) << "All nodes will be restarted";
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    test_cluster()->mini_tablet_server(i)->Shutdown();
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->Start());
    ASSERT_OK(test_cluster()->mini_tablet_server(i)->WaitStarted());
  }
  LOG(INFO) << "All nodes restarted";

  int64 num_intents_after_restart;
  PollForIntentCount(
      initial_num_intents, 0, IntentCountCompareOption::EqualTo, &num_intents_after_restart);
  LOG(INFO) << "Number of intents after tablet split: " << num_intents_after_restart;
  ASSERT_EQ(num_intents_after_restart, initial_num_intents);

  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 100);
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestCheckpointPersistencyAfterTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));
  ASSERT_OK(WriteRowsHelper(200, 300, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTransactionInsertAfterTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_split.size(), num_tablets * 2);

  ASSERT_OK(WriteRowsHelper(200, 300, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> first_tablet_after_split;
  first_tablet_after_split.CopyFrom(tablets_after_split);
  ASSERT_EQ(first_tablet_after_split[0].tablet_id(), tablets_after_split[0].tablet_id());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> second_tablet_after_split;
  second_tablet_after_split.CopyFrom(tablets_after_split);
  second_tablet_after_split.DeleteSubrange(0, 1);
  ASSERT_EQ(second_tablet_after_split.size(), 1);
  ASSERT_EQ(second_tablet_after_split[0].tablet_id(), tablets_after_split[1].tablet_id());

  GetChangesResponsePB change_resp_2 = ASSERT_RESULT(
      GetChangesFromCDC(stream_id, first_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "Number of records from GetChanges() call on first tablet after split: "
            << change_resp_2.cdc_sdk_proto_records_size();

  GetChangesResponsePB change_resp_3 = ASSERT_RESULT(
      GetChangesFromCDC(stream_id, second_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "Number of records from GetChanges() call on second tablet after split: "
            << change_resp_3.cdc_sdk_proto_records_size();

  ASSERT_GE(
      change_resp_2.cdc_sdk_proto_records_size() + change_resp_3.cdc_sdk_proto_records_size(), 100);
}

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetChangesReportsTabletSplitErrorOnRetries)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version=*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  for (int i = 1; i <= 50; i++) {
    ASSERT_OK(WriteRowsHelper(i * 100, (i + 1) * 100, &test_cluster_, true));
  }
  ASSERT_OK(test_client()->FlushTables(
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
  // take some time for the child tablets to be in tunning state.
  ASSERT_OK(SplitTablet(tablets.Get(0).tablet_id(), &test_cluster_));

  // Verify that we did not get the tablet split error in the first 'GetChanges' call
  auto change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &new_checkpoint));

  // Keep calling 'GetChange' until we get an error for the tablet split, this will only happen
  // after both the child tablets are in running state.
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

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetChangesAfterTabletSplitWithMasterShutdown)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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
  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 200);
  LOG(INFO) << "Number of records after restart: " << change_resp_1.cdc_sdk_proto_records_size();

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestGetChangesOnChildrenOnSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(
    CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestGetChangesOnParentTabletAfterTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 200);
  LOG(INFO) << "Number of records after restart: " << change_resp_1.cdc_sdk_proto_records_size();

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestGetChangesMultipleStreamsTabletSplit)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id_1 = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  xrepl::StreamId stream_id_2 = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_1, tablets));
  ASSERT_FALSE(resp.has_error());
  resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id_2, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets));
  GetChangesResponsePB change_resp_2 = ASSERT_RESULT(GetChangesFromCDC(stream_id_2, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(WriteRowsHelper(0, 100, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));

  // Call GetChanges only on one stream so that the other stream will be lagging behind.
  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  ASSERT_OK(WriteRowsHelper(100, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));

  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id_1, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 100);
  LOG(INFO) << "Number of records on first stream after split: "
            << change_resp_1.cdc_sdk_proto_records_size();

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id_1, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  // Calling GetChanges on stream 2 should still return around 200 records.
  change_resp_2 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id_2, tablets, &change_resp_2.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_2.cdc_sdk_proto_records_size(), 200);

  ASSERT_NOK(GetChangesFromCDC(stream_id_2, tablets, &change_resp_2.cdc_sdk_checkpoint()));
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestSetCDCCheckpointAfterTabletSplit)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

  ASSERT_OK(WriteRowsHelper(0, 1000, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

// TODO Adithya: This test is failing in alma linux with clang builds.
TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST(TestTabletSplitBeforeBootstrap)) {
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
  ASSERT_OK(test_client()->FlushTables(
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
  for (auto row_result : ASSERT_RESULT(
           cdc_state_table.GetTableRange(CDCStateTableEntrySelector().IncludeCheckpoint(), &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    const auto& checkpoint = *row.checkpoint;
    LOG(INFO) << "Read cdc_state table row for tablet_id: " << row.key.tablet_id
              << " and stream_id: " << row.key.stream_id << ", with checkpoint: " << checkpoint;

    if (row.key.tablet_id != tablets[0].tablet_id()) {
      // Both children should have the min OpId(-1.-1) as the checkpoint.
      ASSERT_EQ(checkpoint, OpId::Invalid());
    }
    seen_rows += 1;
  }
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

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestCDCStateTableAfterTabletSplit)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

// TODO Adithya: This test is failing in alma linux with clang builds.
TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST(TestCDCStateTableAfterTabletSplitReported)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 200);
  LOG(INFO) << "Number of records after restart: " << change_resp_1.cdc_sdk_proto_records_size();

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_split, /* partition_list_version =*/nullptr));

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run.
  SleepFor(MonoDelta::FromSeconds(2));

  bool saw_row_child_one = false;
  bool saw_row_child_two = false;
  bool saw_row_parent = false;
  // We should still see the entry corresponding to the parent tablet.
  TabletId parent_tablet_id = tablets[0].tablet_id();
  CDCStateTable cdc_state_table(test_client());
  Status s;
  for (auto row_result :
       ASSERT_RESULT(cdc_state_table.GetTableRange({} /* just key columns */, &s))) {
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

  ASSERT_TRUE(saw_row_child_one && saw_row_child_two && saw_row_parent);

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> first_tablet_after_split;
  first_tablet_after_split.CopyFrom(tablets_after_split);
  ASSERT_EQ(first_tablet_after_split[0].tablet_id(), tablets_after_split[0].tablet_id());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> second_tablet_after_split;
  second_tablet_after_split.CopyFrom(tablets_after_split);
  second_tablet_after_split.DeleteSubrange(0, 1);
  ASSERT_EQ(second_tablet_after_split.size(), 1);
  ASSERT_EQ(second_tablet_after_split[0].tablet_id(), tablets_after_split[1].tablet_id());

  GetChangesResponsePB change_resp_2 = ASSERT_RESULT(
      GetChangesFromCDC(stream_id, first_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "Number of records from GetChanges() call on first tablet after split: "
            << change_resp_2.cdc_sdk_proto_records_size();
  ASSERT_RESULT(
      GetChangesFromCDC(stream_id, first_tablet_after_split, &change_resp_2.cdc_sdk_checkpoint()));

  GetChangesResponsePB change_resp_3 = ASSERT_RESULT(
      GetChangesFromCDC(stream_id, second_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "Number of records from GetChanges() call on second tablet after split: "
            << change_resp_3.cdc_sdk_proto_records_size();
  ASSERT_RESULT(
      GetChangesFromCDC(stream_id, second_tablet_after_split, &change_resp_3.cdc_sdk_checkpoint()));

  // Wait until the 'cdc_parent_tablet_deletion_task_' has run. Then the parent tablet's entry
  // should be removed from 'cdc_state' table.
  SleepFor(MonoDelta::FromSeconds(2));

  saw_row_child_one = false;
  saw_row_child_two = false;
  // We should no longer see the entry corresponding to the parent tablet.
  for (const auto& row_result :
       ASSERT_RESULT(cdc_state_table.GetTableRange({} /* just key columns */, &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    LOG(INFO) << "Read cdc_state table row with tablet_id: " << row.key.tablet_id
              << " stream_id: " << row.key.stream_id;

    ASSERT_TRUE(parent_tablet_id != row.key.tablet_id);

    if (row.key.tablet_id == tablets_after_split[0].tablet_id()) {
      saw_row_child_one = true;
    } else if (row.key.tablet_id == tablets_after_split[1].tablet_id()) {
      saw_row_child_two = true;
    }
  }
}

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCAfterTabletSplitReported)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 200);
  LOG(INFO) << "Number of records after restart: " << change_resp_1.cdc_sdk_proto_records_size();

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "The tablet split error is now communicated to the client.";

  auto get_tablets_resp =
      ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id, tablets[0].tablet_id()));
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 2);

  // Assert that GetTabletListToPollForCDC also populates the snapshot_time.
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    ASSERT_GE(tablet_checkpoint_pair.cdc_sdk_checkpoint().snapshot_time(), 0);
  }

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

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCBeforeTabletSplitReported)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCBootstrapWithTabletSplit)) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCBootstrapWithTwoTabletSplits)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));
  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);
  LOG(INFO) << "First tablet split succeded on tablet: " << tablets[0].tablet_id();

  ASSERT_OK(WriteRowsHelper(200, 400, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCWithTwoTabletSplits)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRowsHelper(200, 400, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets_after_first_split, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(0).tablet_id(), table, 3);

  change_resp_1 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 200);
  LOG(INFO) << "Number of records after restart: " << change_resp_1.cdc_sdk_proto_records_size();

  // We are calling: "GetTabletListToPollForCDC" when the tablet split on the parent tablet has
  // still not been communicated to the client. Hence we should get only the original parent tablet.
  auto get_tablets_resp = ASSERT_RESULT(GetTabletListToPollForCDC(stream_id, table_id));
  ASSERT_EQ(get_tablets_resp.tablet_checkpoint_pairs().size(), 1);
  for (const auto& tablet_checkpoint_pair : get_tablets_resp.tablet_checkpoint_pairs()) {
    const auto& tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    ASSERT_EQ(tablet_id, tablets[0].tablet_id());
  }

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));

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

  change_resp_1 = ASSERT_RESULT(
      GetChangesFromCDC(stream_id, tablets_after_first_split, &change_resp_1.cdc_sdk_checkpoint()));
  ASSERT_NOK(
      GetChangesFromCDC(stream_id, tablets_after_first_split, &change_resp_1.cdc_sdk_checkpoint()));
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTabletSplitOnAddedTableForCDC)) {
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
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

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

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true, 2, "test_table_1"));
  ASSERT_OK(test_client()->FlushTables(
      {table_2_id}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  WaitUntilSplitIsSuccesful(tablets_2.Get(0).tablet_id(), table_2);

  // Verify GetChanges returns records even after tablet split, i.e tablets of the newly added table
  // are hidden instead of being deleted.
  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_2, &change_resp.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp.cdc_sdk_proto_records_size(), 200);

  // Now that all the required records have been streamed, verify that the tablet split error is
  // reported.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets_2, &change_resp.cdc_sdk_checkpoint()));
}

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestTabletSplitOnAddedTableForCDCWithMasterRestart)) {
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
  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

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
  ASSERT_OK(test_client()->FlushTables(
      {table_2_id}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  WaitUntilSplitIsSuccesful(tablets_2.Get(0).tablet_id(), table_2);

  // Verify GetChanges returns records even after tablet split, i.e tablets of the newly added table
  // are hidden instead of being deleted.
  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets_2, &change_resp.cdc_sdk_checkpoint()));
  ASSERT_GE(change_resp.cdc_sdk_proto_records_size(), 200);

  // Now that all the required records have been streamed, verify that the tablet split error is
  // reported.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets_2, &change_resp.cdc_sdk_checkpoint()));
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());
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

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTransactionCommitAfterTabletSplit)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  // Initiate a transaction.
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("BEGIN"));

  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table}, /* add_indexes = */ false, /* timeout_secs = */ 30,
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
  ASSERT_OK(test_client()->FlushTables(
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

  uint32_t child1_record_count = GetTotalNumRecordsInTablet(
      stream_id, first_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint());
  uint32_t child2_record_count = GetTotalNumRecordsInTablet(
      stream_id, second_tablet_after_split, &change_resp_1.cdc_sdk_checkpoint());

  ASSERT_GE(child1_record_count + child2_record_count, 200);
}

TEST_F(
    CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestTabletSplitBeforeBootstrapGetCheckpoint)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCWithOnlyOnePolledChild)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
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

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestRecordCountsAfterMultipleTabletSplits)) {
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
  ASSERT_OK(WriteRows(1, 200, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 100, false));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRows(200, 400, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(0).tablet_id(), table, 3);
  WaitUntilSplitIsSuccesful(tablets_after_first_split.Get(1).tablet_id(), table, 4);

  ASSERT_OK(WriteRows(400, 600, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_third_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_third_split, nullptr));
  ASSERT_EQ(tablets_after_third_split.size(), 4);

  WaitUntilSplitIsSuccesful(tablets_after_third_split.Get(1).tablet_id(), table, 5);

  ASSERT_OK(WriteRows(600, 1000, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 100, false));

  const int expected_total_records = 3006;
  const int expected_total_splits = 4;
  // The array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in that
  // order.
  const int expected_records_count[] = {9, 999, 0, 0, 0, 0, 999, 999};

  int total_records = 0;
  int total_splits = 0;
  int record_count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  GetRecordsAndSplitCount(
      stream_id, tablets[0].tablet_id(), table_id, record_count, &total_records, &total_splits);

  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_records_count[i], record_count[i]);
  }

  LOG(INFO) << "Got " << total_records << " records and " << total_splits << " tablet splits";
  ASSERT_EQ(expected_total_records, total_records);
  ASSERT_EQ(expected_total_splits, total_splits);
}

TEST_F(
    CDCSDKTabletSplitTest,
    YB_DISABLE_TEST_IN_TSAN(TestRecordCountsAfterMultipleTabletSplitsInMultiNodeCluster)) {
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

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRows(1, 200, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 100, false));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WriteRows(200, 400, &test_cluster_));
  ASSERT_OK(test_client()->FlushTables({table.table_id()}, false, 100, false));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_after_first_split;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets_after_first_split, nullptr));
  ASSERT_EQ(tablets_after_first_split.size(), 2);

  const int expected_total_records = 1200;
  const int expected_total_splits = 1;
  // The array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE, BEGIN, COMMIT in that
  // order.
  const int expected_records_count[] = {3, 399, 0, 0, 0, 0, 399, 399};

  int total_records = 0;
  int total_splits = 0;
  int record_count[] = {0, 0, 0, 0, 0, 0, 0, 0};

  GetRecordsAndSplitCount(
      stream_id, tablets[0].tablet_id(), table_id, record_count, &total_records, &total_splits);

  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(expected_records_count[i], record_count[i]);
  }

  LOG(INFO) << "Got " << total_records << " records and " << total_splits << " tablet splits";
  ASSERT_EQ(expected_total_records, total_records);
  ASSERT_EQ(expected_total_splits, total_splits);
}

TEST_F(CDCSDKTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(TestGetTabletListToPollForCDCWithTabletId)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  const uint32_t num_tablets = 1;

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(resp.has_error());
  GetChangesResponsePB change_resp_1 = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  ASSERT_OK(WriteRowsHelper(1, 200, &test_cluster_, true));
  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
      /* is_compaction = */ true));
  std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_aborted_intent_cleanup_ms));
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  SleepFor(MonoDelta::FromSeconds(30));

  WaitUntilSplitIsSuccesful(tablets.Get(0).tablet_id(), table);

  ASSERT_OK(WaitForGetChangesToFetchRecords(
      &change_resp_1, stream_id, tablets, 199, &change_resp_1.cdc_sdk_checkpoint()));

  ASSERT_GE(change_resp_1.cdc_sdk_proto_records_size(), 200);
  LOG(INFO) << "Number of records after restart: " << change_resp_1.cdc_sdk_proto_records_size();

  // Now that there are no more records to stream, further calls of 'GetChangesFromCDC' to the same
  // tablet should fail.
  ASSERT_NOK(GetChangesFromCDC(stream_id, tablets, &change_resp_1.cdc_sdk_checkpoint()));
  LOG(INFO) << "The tablet split error is now communicated to the client.";

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

#ifndef NDEBUG
TEST_F(CDCSDKTabletSplitTest, TestCleanUpCDCStreamsMetadataDuringTabletSplit) {
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
  int idx = 0;
  vector<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> tablets(kNumTables);

  for (auto table_suffix : table_list_suffix) {
    table[idx] = ASSERT_RESULT(CreateTable(
        &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, true, table_suffix));
    ASSERT_OK(test_client()->GetTablets(
        table[idx], 0, &tablets[idx], /* partition_list_version = */ nullptr));
    ASSERT_OK(WriteEnumsRows(
        0 /* start */, 100 /* end */, &test_cluster_, table_suffix, kNamespaceName, kTableName));
    idx += 1;
  }
  auto stream_id = ASSERT_RESULT(CreateDBStream(IMPLICIT));

  // Consume changes for table 1 whose tablet will be split later. It ensures that the parent tablet
  // remains in the cdc_state table after the split is complete.
  auto resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets[0]));
  ASSERT_FALSE(resp.has_error());
  ASSERT_OK(test_client()->FlushTables(
      table, /* add_indexes = */ false, /* timeout_secs = */ 30,
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
#endif

}  // namespace cdc
}  // namespace yb
