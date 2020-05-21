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
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/txn-test-base.h"

#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/integration-tests/cdc_test_util.h"

#include "yb/rpc/messenger.h"
#include "yb/tablet/tablet.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/slice.h"

namespace yb {
namespace cdc {

using client::Flush;
using client::TransactionTestBase;
using client::WriteOpType;
using rpc::RpcController;

class CDCServiceTxnTest : public TransactionTestBase {
 protected:
  void SetUp() override {
    mini_cluster_opt_.num_masters = 1;
    mini_cluster_opt_.num_tablet_servers = 1;
    create_table_ = false;
    SetIsolationLevel(IsolationLevel::SERIALIZABLE_ISOLATION);
    TransactionTestBase::SetUp();
    client::KeyValueTableTest::CreateTable(
        client::Transactional::kTrue, 1 /* num tablets */, client_.get(), &table_);

    const auto mini_server = cluster_->mini_tablet_servers().front();
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(), HostPort::FromBoundEndpoint(mini_server->bound_rpc_addr()));
  }

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
};

void AssertValue(const google::protobuf::Map<string, QLValuePB>& changes, int32_t expected_value) {
  ASSERT_EQ(changes.size(), 1);
  const auto& value = changes.find("value");
  ASSERT_NE(value, changes.end());
  ASSERT_EQ(value->second.int32_value(), expected_value);
}

TEST_F(CDCServiceTxnTest, TestGetChanges) {
  // Consider the following writes:
  // TO: WRITE K0
  // T1: WRITE K1 (TXN1)
  // T2: WRITE K2 (TXN2)
  // T3: WRITE K3
  // T4: APPLYING TXN2
  // T5: APPLYING TXN1
  // T6: WRITE K4
  // The order in which keys are written to DB (and should be read by CDC) is K0, K3, K2, K1, K4.

  auto session = CreateSession();
  ASSERT_RESULT(WriteRow(session, 10000 /* key */, 10000 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));

  auto txn1 = CreateTransaction();
  auto session1 = CreateSession(txn1);
  ASSERT_RESULT(WriteRow(session1, 10001 /* key */, 10001 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));

  auto txn2 = CreateTransaction();
  auto session2 = CreateSession(txn2);
  ASSERT_RESULT(WriteRow(session2, 10002 /* key */, 10002 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));

  ASSERT_RESULT(WriteRow(session, 10003 /* key */, 10003 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));

  ASSERT_OK(txn2->CommitFuture().get());
  ASSERT_OK(txn1->CommitFuture().get());

  ASSERT_RESULT(WriteRow(session, 10004 /* key */, 10004 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));

  // Get tablet ID.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(table_->name(), 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_stream_id(stream_id);
  change_req.set_tablet_id(tablets.Get(0).tablet_id());
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // Expect total 7 records: 5 WRITE_OP records and 2 UPDATE_TRANSACTION_OP records.
    ASSERT_EQ(change_resp.records_size(), 7);

    // Expected order of results is K0, K3, K2, <UPDATE_TXN_OP>, K1, <UPDATE_TXN_OP>, K4.
    int32_t expected_order[7] = {10000, 10003, 10002, 0, 10001, 0, 10004};

    for (int i = 0; i < 7; i++) {
      if (i == 3 || i == 5) {
        // This contains the record for APPLYING transaction.
        ASSERT_EQ(change_resp.records(i).changes_size(), 0);
      } else {
        ASSERT_EQ(change_resp.records(i).changes_size(), 1);

        // Check the key.
        ASSERT_NO_FATALS(AssertIntKey(change_resp.records(i).key(), expected_order[i]));
      }
    }
  }
}

TEST_F(CDCServiceTxnTest, TestGetChangesForPendingTransaction) {
  // If GetChanges is called in the middle of a transaction, ensure that transaction is not
  // incorrectly considered as aborted if we can't find the transaction commit record.
  // A subsequent call to GetChanges after the transaction is committed should get the
  // rows committed by transaction.

  // Get tablet ID.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(table_->name(), 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);

  // Create CDC stream on table.
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);
  ASSERT_RESULT(WriteRow(session, 10001 /* key */, 10001 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));
  ASSERT_RESULT(WriteRow(session, 10002 /* key */, 10002 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));
  ASSERT_RESULT(WriteRow(session, 10003 /* key */, 10003 /* value */, WriteOpType::INSERT,
                         Flush::kTrue));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_stream_id(stream_id);
  change_req.set_tablet_id(tablets.Get(0).tablet_id());
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // Expect 0 records because transaction is not yet committed.
    ASSERT_EQ(change_resp.records_size(), 0);
  }

  // Commit transaction.
  ASSERT_OK(txn->CommitFuture().get());
  ASSERT_OK(session->Flush());

  // Get CDC changes.
  {
    // Need to poll because Flush returns on majority_replicated and CDC waits for fully committed.
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      RpcController rpc;
      change_resp.Clear();
      SCOPED_TRACE(change_req.DebugString());
      RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
      SCOPED_TRACE(change_resp.DebugString());
      if (change_resp.has_error()) return Result<bool>(StatusFromPB(change_resp.error().status()));

      // Expect at least 4 records because transaction is now committed: 3 rows + txn commit.
      return change_resp.records_size() == 4;
    }, MonoDelta::FromSeconds(30), "Wait for Transaction to be committed."));

    int32_t expected_order[3] = {10001, 10002, 10003};

    for (int i = 0; i < 3; i++) {
      ASSERT_EQ(change_resp.records(i).changes_size(), 1);
      // Check the key.
      ASSERT_NO_FATALS(AssertIntKey(change_resp.records(i).key(), expected_order[i]));
    }

    // Check the APPLYING transaction record.
    ASSERT_EQ(change_resp.records(3).changes_size(), 0);
    ASSERT_TRUE(change_resp.records(3).has_transaction_state());
  }
}

} // namespace cdc
} // namespace yb
