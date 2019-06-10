// Copyright (c) YugaByte, Inc.

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/txn-test-base.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"
#include "yb/rpc/messenger.h"
#include "yb/tablet/tablet.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/cdc_test_util.h"
#include "yb/util/slice.h"

namespace yb {
namespace cdc {

using client::Flush;
using client::TransactionTestBase;
using client::WriteOpType;
using rpc::RpcController;

class CDCServiceTxnTest : public TransactionTestBase {
 protected:
  IsolationLevel GetIsolationLevel() override {
    return IsolationLevel::SERIALIZABLE_ISOLATION;
  }

 protected:
  void SetUp() override {
    mini_cluster_opt_.num_masters = 1;
    mini_cluster_opt_.num_tablet_servers = 1;
    create_table_ = false;
    TransactionTestBase::SetUp();
    client::KeyValueTableTest::CreateTable(
        client::Transactional::kTrue, 1 /* num tablets */, client_.get(), &table_);

    const auto mini_server = cluster_->mini_tablet_servers().front();
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(), HostPort::FromBoundEndpoint(mini_server->bound_rpc_addr()));
  }

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
};

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

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablets.Get(0).tablet_id());
  change_req.set_subscriber_uuid("subscriber_test");
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

} // namespace cdc
} // namespace yb
