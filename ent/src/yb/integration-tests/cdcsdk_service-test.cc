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

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.proxy.h"

#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/txn-test-base.h"

#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/integration-tests/cdc_test_util.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/tablet.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/slice.h"

DECLARE_bool(cdc_enable_replicate_intents);

namespace yb {
namespace cdc {
using client::Flush;
using client::TransactionTestBase;
using client::WriteOpType;
using rpc::RpcController;

struct Record {
  int32_t key;    // 0 if WRITE or DDL record
  int32_t value;  // 0 if WRITE, DELETE or DDL record.
  bool isDDL;     // true if DDL record.
  bool isInsert;  // true if INSERT record
  bool isDelete;  // true if DELETE record
  bool isUpdate;  // true if UPDATE record
  bool isWrite;   // true if WRITE record
};

// todo (Vaibhav): this test file needs to be revisited once complete CDC support for YCQL lands
class CDCSDKServiceTest : public TransactionTestBase<MiniCluster>,
                          public testing::WithParamInterface<bool /* enable_intents */> {
 protected:
  void SetUp() override {
    mini_cluster_opt_.num_masters = 1;
    mini_cluster_opt_.num_tablet_servers = 1;
    SetAtomicFlag(0, &FLAGS_cdc_enable_replicate_intents);
    create_table_ = false;
    SetIsolationLevel(IsolationLevel::SERIALIZABLE_ISOLATION);
    SetNumTablets(1);
    TransactionTestBase::SetUp();
    CreateTable();

    const auto mini_server = cluster_->mini_tablet_servers().front();
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(), HostPort::FromBoundEndpoint(mini_server->bound_rpc_addr()));
  }

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
};

INSTANTIATE_TEST_CASE_P(EnableIntentReplication, CDCSDKServiceTest, ::testing::Bool());

void AssertKeyValue(
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& key,
    int32_t expected_key,
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& changes,
    int32_t expected_value) {
  ASSERT_EQ(key.size(), 1);
  ASSERT_EQ(key[0].key(), "key");
  ASSERT_EQ(key[0].value().int32_value(), expected_key);

  ASSERT_EQ(changes.size(), 1);
  ASSERT_EQ(changes[0].key(), "value");
  ASSERT_EQ(changes[0].value().int32_value(), expected_value);
}

void checkDDLRecord(const CDCSDKRecordPB& record, Record expectedRecord) {
  ASSERT_EQ(CDCSDKRecordPB::DDL, record.operation());
  ASSERT_TRUE(record.has_schema());
}

void checkInsertRecord(const CDCSDKRecordPB& record, Record expectedRecord) {
  ASSERT_EQ(CDCSDKRecordPB::INSERT, record.operation());
  ASSERT_NO_FATALS(
      AssertKeyValue(record.key(), expectedRecord.key, record.changes(), expectedRecord.value));
}

void checkDeleteRecord(const CDCSDKRecordPB& record, Record expectedRecord) {
  ASSERT_EQ(CDCSDKRecordPB::DELETE, record.operation());
  ASSERT_NO_FATALS(AssertIntKey(record.key(), expectedRecord.key));

  if (record.has_transaction_state()) {
    const auto& transaction_state = record.transaction_state();
    ASSERT_TRUE(transaction_state.has_transaction_id());
  }
}

void checkUpdateRecord(const CDCSDKRecordPB& record, Record expectedRecord) {
  ASSERT_EQ(CDCSDKRecordPB::UPDATE, record.operation());
  ASSERT_NO_FATALS(
      AssertKeyValue(record.key(), expectedRecord.key, record.changes(), expectedRecord.value));
}

void checkWriteRecord(const CDCSDKRecordPB& record, Record expectedRecord) {
  ASSERT_EQ(CDCSDKRecordPB::WRITE, record.operation());
  ASSERT_TRUE(record.has_transaction_state());
  ASSERT_EQ(record.changes_size(), 0);
}

void checkRecord(const CDCSDKRecordPB& record, Record expectedRecord) {
  if (expectedRecord.isDDL)
    checkDDLRecord(record, expectedRecord);
  else if (expectedRecord.isInsert)
    checkInsertRecord(record, expectedRecord);
  else if (expectedRecord.isDelete)
    checkDeleteRecord(record, expectedRecord);
  else if (expectedRecord.isUpdate)
    checkUpdateRecord(record, expectedRecord);
  else if (expectedRecord.isWrite)
    checkWriteRecord(record, expectedRecord);
}

void prepare_change_req(GetChangesRequestPB* change_req, const CDCStreamId stream_id,
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets) {
  change_req->set_stream_id(stream_id);
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key("");
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(0);
}

/*
 * CDC Behaviour:
 * - DDL are streamed twice (1 in the beginning, 1 in the end)
 * - WRITE op is streamed when we do a commit operation in the transaction
 *   or if there is a batch insert (tested using Java)
 * - DELETE op is streamed on deletion of a row, the CDC Record only contains the deleted key
 * - UPDATE op is streamed if an update is made to a non-prime attribute
 * - If an update is made to the primary attribute then the order of
 *   records is DELETE + INSERT + WRITE
 * - If an update is made to the primary attribute then the order of
 *   records is DELETE + INSERT + WRITE
 * - Looping through the record_order array ensures that the matches are done in order only
     thus effectively checking that the records are coming in order too
 */

// Insert a single row outside an explicit transaction.
// Expected records: 3 (DDL, INSERT, DDL).
TEST_P(CDCSDKServiceTest, SingleShardInsertWithAutoCommit) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto session = CreateSession();

  ASSERT_RESULT(
      WriteRow(session, 2000 /* key */, 2001 /* value */, WriteOpType::INSERT, Flush::kTrue));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  prepare_change_req(&change_req, stream_id, tablets);

  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 2);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[3] = {
        {0, 0, true, false, false, false, false},
        {2000, 2001, false, true, false, false, false}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Begin transaction, insert a single row, commit.
// Expected records: 4 (DDL, INSERT, WRITE, DDL).
TEST_P(CDCSDKServiceTest, InsertSingleRow) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);

  ASSERT_RESULT(
      WriteRow(session, 2000 /* key */, 2001 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_OK(txn->CommitFuture().get());

  sleep(5);
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // Record size is expected to be 4 -- 1 HEADER DATA, 1 WRITE_OP, 1 UPDATE_TRANSACTION_OP
    // and 1 CHANGE_METADATA_OP.
    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 3);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[6] = {
        {0, 0, true, false, false, false, false},
        {2000, 2001, false, true, false, false, false},
        {0, 0, false, false, false, false, true}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Insert 4 rows outside an explicit transaction.
// Expected records: 6 (DDL, 4_INSERT, DDL).
TEST_P(CDCSDKServiceTest, SingleShardInsert4Rows) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto session = CreateSession();

  ASSERT_RESULT(
      WriteRow(session, 2003 /* key */, 2003 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2004 /* key */, 2004 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2005 /* key */, 2005 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2006 /* key */, 2006 /* value */, WriteOpType::INSERT, Flush::kTrue));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 5);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[6] = {{0, 0, true, false, false, false, false},
                              {2003, 2003, false, true, false, false, false},
                              {2004, 2004, false, true, false, false, false},
                              {2005, 2005, false, true, false, false, false},
                              {2006, 2006, false, true, false, false, false}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Begin transaction, insert 4 rows one by one, commit.
// Expected records: 7 (DDL, 4_INSERT, WRITE, DDL).
TEST_P(CDCSDKServiceTest, Insert4Rows) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);

  ASSERT_RESULT(
      WriteRow(session, 2003 /* key */, 2003 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2004 /* key */, 2004 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2005 /* key */, 2005 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2006 /* key */, 2006 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_OK(txn->CommitFuture().get());

  sleep(5);
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 6);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[7] = {{0, 0, true, false, false, false, false},
                              {2003, 2003, false, true, false, false, false},
                              {2004, 2004, false, true, false, false, false},
                              {2005, 2005, false, true, false, false, false},
                              {2006, 2006, false, true, false, false, false},
                              {0, 0, false, false, false, false, true}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Write a single row, update its value outside an explicit transaction.
// Expected records: 4 (DDL, INSERT, UPDATE, DDL).
TEST_P(CDCSDKServiceTest, SingleShardUpdateWithAutoCommit) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto session = CreateSession();

  ASSERT_RESULT(
      WriteRow(session, 2000 /* key */, 2001 /* value */, WriteOpType::INSERT, Flush::kTrue));

  ASSERT_RESULT(UpdateRow(session, 2000 /* key */, 2002 /* value */));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 3);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[4] = {
        {0, 0, true, false, false, false, false},
        {2000, 2001, false, true, false, false, false},
        {2000, 2002, false, false, false, true, false}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Begin transaction, insert one row, update the inserted row.
// Expected records: 6 (DDL, INSERT, WRITE, UPDATE, WRITE, DDL).
TEST_P(CDCSDKServiceTest, UpdateInsertedRow) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);

  ASSERT_RESULT(
      WriteRow(session, 2003 /* key */, 2003 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_OK(txn->CommitFuture().get());
  sleep(5);

  auto txn2 = CreateTransaction();
  auto session2 = CreateSession(txn2);

  ASSERT_RESULT(UpdateRow(session2, 2003 /* key */, 2004 /* value */));
  ASSERT_OK(txn2->CommitFuture().get());
  sleep(5);

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 5);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[6] = {
        {0, 0, true, false, false, false, false}, {2003, 2003, false, true, false, false, false},
        {0, 0, false, false, false, false, true}, {2003, 2004, false, false, false, true, false},
        {0, 0, false, false, false, false, true}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Begin transaction, insert 3 rows, commit, update 2 of them.
// Expected records: 9 (DDL, 3_INSERT, WRITE, 2_UPDATE, WRITE, DDL).
TEST_P(CDCSDKServiceTest, UpdateMultipleRows) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);

  ASSERT_RESULT(
      WriteRow(session, 2003 /* key */, 2003 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2004 /* key */, 2004 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2005 /* key */, 2005 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_OK(txn->CommitFuture().get());
  sleep(5);

  auto txn2 = CreateTransaction();
  auto session2 = CreateSession(txn2);

  ASSERT_RESULT(UpdateRow(session2, 2003 /* key */, 20030 /* value */));
  ASSERT_RESULT(UpdateRow(session2, 2004 /* key */, 20040 /* value */));
  ASSERT_OK(txn2->CommitFuture().get());
  sleep(5);

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 8);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[9] = {{0, 0, true, false, false, false, false},
                              {2003, 2003, false, true, false, false, false},
                              {2004, 2004, false, true, false, false, false},
                              {2005, 2005, false, true, false, false, false},
                              {0, 0, false, false, false, false, true},
                              {2003, 20030, false, false, false, true, false},
                              {2004, 20040, false, false, false, true, false},
                              {0, 0, false, false, false, false, true}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Insert 3 rows, update all of them outside an explicit transaction.
// Expected records: 8 (DDL, 3_INSERT, 3_UPDATE, DDL).
TEST_P(CDCSDKServiceTest, SingleShardUpdateRows) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto session = CreateSession();

  ASSERT_RESULT(
      WriteRow(session, 2003 /* key */, 2003 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2004 /* key */, 2004 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 2005 /* key */, 2005 /* value */, WriteOpType::INSERT, Flush::kTrue));

  auto session2 = CreateSession();

  ASSERT_RESULT(UpdateRow(session2, 2003 /* key */, 20030 /* value */));
  ASSERT_RESULT(UpdateRow(session2, 2004 /* key */, 20040 /* value */));
  ASSERT_RESULT(UpdateRow(session2, 2005 /* key */, 20050 /* value */));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 7);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[8] = {{0, 0, true, false, false, false, false},
                              {2003, 2003, false, true, false, false, false},
                              {2004, 2004, false, true, false, false, false},
                              {2005, 2005, false, true, false, false, false},
                              {2003, 20030, false, false, false, true, false},
                              {2004, 20040, false, false, false, true, false},
                              {2005, 20050, false, false, false, true, false}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Write a single row, delete it outside an explicit transaction.
// Expected records: 4 (DDL, INSERT, DELETE, DDL).
TEST_P(CDCSDKServiceTest, SingleShardDeleteWithAutoCommit) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto session = CreateSession();

  ASSERT_RESULT(
      WriteRow(session, 2000 /* key */, 2001 /* value */, WriteOpType::INSERT, Flush::kTrue));

  ASSERT_RESULT(DeleteRow(session, 2000 /* key */));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 3);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[4] = {
        {0, 0, true, false, false, false, false},
        {2000, 2001, false, true, false, false, false},
        {2000, 0, false, false, true, false, false}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Begin transaction, Begin transaction, insert one row, commit, begin new transaction,
// delete inserted row, commit.
// Expected records: 6 (DDL. INSERT, WRITE, DELETE, WRITE, DDL).
TEST_P(CDCSDKServiceTest, DeleteInsertedRow) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);

  ASSERT_RESULT(
      WriteRow(session, 2002 /* key */, 2002 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_OK(txn->CommitFuture().get());
  sleep(5);

  auto txn2 = CreateTransaction();
  auto session2 = CreateSession(txn2);

  ASSERT_RESULT(DeleteRow(session2, 2002 /* key */));
  ASSERT_OK(txn2->CommitFuture().get());
  sleep(5);

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);

  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 5);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[6] = {
        {0, 0, true, false, false, false, false}, {2002, 2002, false, true, false, false, false},
        {0, 0, false, false, false, false, true}, {2002, 0, false, false, true, false, false},
        {0, 0, false, false, false, false, true}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}

// Begin transaction, perform some operations and abort the transaction.
// Expected records: 1 (DDL).
TEST_P(CDCSDKServiceTest, AbortAllWriteOperations) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
    table_->name(), 0, &tablets,
    /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  RpcController rpc;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id, CDCSDK);

  SetCDCCheckpointRequestPB set_checkpoint_req;
  SetCDCCheckpointResponsePB set_checkpoint_resp;
  set_checkpoint_req.set_stream_id(stream_id);
  set_checkpoint_req.set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req.mutable_checkpoint()->mutable_op_id()->set_index(0);
  ASSERT_OK(cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &rpc));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);

  ASSERT_RESULT(
      WriteRow(session, 20000 /* key */, 20000 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 20001 /* key */, 20001 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 20002 /* key */, 20002 /* value */, WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(
      WriteRow(session, 20003 /* key */, 20003 /* value */, WriteOpType::INSERT, Flush::kTrue));

  ASSERT_NO_FATALS(txn->Abort());

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  prepare_change_req(&change_req, stream_id, tablets);
  {
    RpcController get_changes_rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    ASSERT_EQ(change_resp.cdc_sdk_records_size(), 1);

    int recordSize = change_resp.cdc_sdk_records_size();

    /* isDDL, isInsert, isDelete, isUpdate, isWrite */
    Record record_order[1] = {{0, 0, true, false, false, false, false}};

    for (int i = 0; i < recordSize; ++i) {
      checkRecord(change_resp.cdc_sdk_records(i), record_order[i]);
    }
  }
}
}  // namespace cdc
}  // namespace yb
