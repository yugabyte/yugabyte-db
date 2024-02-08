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

#include <algorithm>
#include <chrono>
#include <boost/assign.hpp>
#include "yb/util/flags.h"
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/atomic.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

class CDCStateTableTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override { CDCSDKYsqlTest::SetUp(); }
};

TEST_F(CDCStateTableTest, TestUpdateEntriesWithReplaceMapFalse) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  ASSERT_OK(cdc_state_table.InsertEntries({*entry_opt}));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << " and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try updating the entry with replace_map = false
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  ASSERT_OK(cdc_state_table.UpdateEntries({entry}, false /* replace_map */));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
  // cdc_sdk_safe_time should not have changed
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
}

TEST_F(CDCStateTableTest, TestUpdateEntriesWithReplaceMapTrue) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  ASSERT_OK(cdc_state_table.InsertEntries({*entry_opt}));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << " and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try updating the entry with replace_map = true
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  ASSERT_OK(cdc_state_table.UpdateEntries({entry}, true /* replace_map */));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  // cdc_sdk_safe_time should not exist (since replace_map = true)
  ASSERT_FALSE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
}

TEST_F(CDCStateTableTest, TestUpsertEntriesWithReplaceMapFalse) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  // Should insert a new row since row does not exist
  ASSERT_OK(cdc_state_table.UpsertEntries({*entry_opt}, false /* replace_map */));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << " and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try updating the entry with replace_map = false
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  ASSERT_OK(cdc_state_table.UpsertEntries({entry}, false /* replace_map */));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
  // cdc_sdk_safe_time should not have changed
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
}

TEST_F(CDCStateTableTest, TestUpsertEntriesWithReplaceMapTrue) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  // Should insert a new row since row does not exist
  ASSERT_OK(cdc_state_table.UpsertEntries({*entry_opt}, true /* replace_map */));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << " and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try updating the entry with replace_map = true
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  ASSERT_OK(cdc_state_table.UpsertEntries({entry}, true /* replace_map */));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  // cdc_sdk_safe_time should not exist (since replace_map = true)
  ASSERT_FALSE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
}

TEST_F(CDCStateTableTest, TestUpdateEntriesWithUpdateAndRemoveKeyInSingleBatch) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;
  entry_opt->snapshot_key = "0";

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  ASSERT_OK(cdc_state_table.InsertEntries({*entry_opt}));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << ", cdc_sdk_safe_time = " << cdc_sdk_safe_time_1 << " and snapshot_key = 0";

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_TRUE(entry_opt->snapshot_key.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
  ASSERT_EQ(*entry_opt->snapshot_key, "0");

  // Now try updating the entry along with a delete of snapshot_key from map
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  entry.snapshot_key = "1";
  ASSERT_OK(cdc_state_table.UpdateEntries(
      {entry}, false /* replace_map */, {"snapshot_key"} /* keys_to_delete */));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  // snapshot_key should have been deleted
  ASSERT_FALSE(entry_opt->snapshot_key.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
  // cdc_sdk_safe_time should not have changed
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
}

TEST_F(CDCStateTableTest, TestRemovingNonExistentKeyFromMap) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  ASSERT_OK(cdc_state_table.InsertEntries({*entry_opt}));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << ", and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try updating the entry along with a delete of snapshot_key which is not there in map
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  ASSERT_OK(cdc_state_table.UpdateEntries(
      {entry}, false /* replace_map */, {"snapshot_key"} /* keys_to_delete */));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
  // cdc_sdk_safe_time should not have changed
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
}

TEST_F(CDCStateTableTest, TestInsertEntriesWithSameKeyTwice) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  ASSERT_OK(cdc_state_table.InsertEntries({*entry_opt}));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << ", and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try inserting the entry with same {tablet_id, stream_id} key again with new column values
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  entry.cdc_sdk_safe_time = active_time_2;
  ASSERT_OK(cdc_state_table.InsertEntries({entry}));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should not have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  // cdc_sdk_safe_time should not have changed
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
}

TEST_F(CDCStateTableTest, TestInsertAndUpsertEntriesWithSameKey) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  ASSERT_OK(cdc_state_table.InsertEntries({*entry_opt}));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << ", and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);

  // Now try upserting the entry with same {tablet_id, stream_id} key again with new column values
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_2 = GetCurrentTimeMicros();
  entry.active_time = active_time_2;
  entry.cdc_sdk_safe_time = active_time_2;
  ASSERT_OK(cdc_state_table.UpsertEntries({entry}));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  // active_time should have changed
  ASSERT_EQ(*entry_opt->active_time, active_time_2);
  // cdc_sdk_safe_time should have changed
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, active_time_2);
}

TEST_F(CDCStateTableTest, TestUpdateEntriesWithNoExistingEntry) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());

  // Delete any existing entry and try updating entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_FALSE(entry_opt.has_value());

  // Call UpdateEntries when row does not exist
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_1 = GetCurrentTimeMicros();
  entry.active_time = active_time_1;
  entry.cdc_sdk_safe_time = active_time_1;
  ASSERT_OK(cdc_state_table.UpdateEntries({entry}));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_FALSE(entry_opt.has_value());
}

TEST_F(CDCStateTableTest, TestUpsertEntriesWithNoExistingEntry) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());

  // Delete any existing entry and try upserting entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_FALSE(entry_opt.has_value());

  // Call UpsertEntries when row does not exist
  CDCStateTableEntry entry(tablets[0].tablet_id(), stream_id);
  const auto& active_time_1 = GetCurrentTimeMicros();
  entry.active_time = active_time_1;
  entry.cdc_sdk_safe_time = active_time_1;
  ASSERT_OK(cdc_state_table.UpsertEntries({entry}));

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  // active_time and cdc_sdk_safe_time should have been updated
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, active_time_1);
}

TEST_F(CDCStateTableTest, TestUpsertEntriesWithRemoveKey) {
  ASSERT_OK(SetUpWithParams(1, 1, false));
  const uint32_t num_tablets = 1;
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, num_tablets));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), num_tablets);

  TableId table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));

  xrepl::StreamId stream_id = ASSERT_RESULT(CreateDBStream());

  CDCStateTable cdc_state_table(test_client());

  auto entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(entry_opt.has_value());
  const auto& active_time_1 = GetCurrentTimeMicros();
  const auto& cdc_sdk_safe_time_1 = active_time_1;
  entry_opt->active_time = active_time_1;
  entry_opt->cdc_sdk_safe_time = cdc_sdk_safe_time_1;
  entry_opt->snapshot_key = "0";

  // Delete any existing entry and create a fresh entry in CDC state table
  ASSERT_OK(cdc_state_table.DeleteEntries({entry_opt->key}));
  // Should insert a new row since row does not exist
  ASSERT_OK(cdc_state_table.UpsertEntries(
      {*entry_opt}, false /* replace_map */, {"snapshot_key"} /* keys_to_delete */));

  LOG(INFO) << "Inserted an entry with active_time = " << active_time_1
            << " and cdc_sdk_safe_time = " << cdc_sdk_safe_time_1;

  entry_opt = ASSERT_RESULT(cdc_state_table.TryFetchEntry(
      {tablets[0].tablet_id(), stream_id}, CDCStateTableEntrySelector().IncludeAll()));

  ASSERT_TRUE(entry_opt->active_time.has_value());
  ASSERT_TRUE(entry_opt->cdc_sdk_safe_time.has_value());
  ASSERT_FALSE(entry_opt->snapshot_key.has_value());
  ASSERT_EQ(*entry_opt->active_time, active_time_1);
  ASSERT_EQ(*entry_opt->cdc_sdk_safe_time, cdc_sdk_safe_time_1);
}

}  // namespace cdc
}  // namespace yb
