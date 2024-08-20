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

#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"
#include "yb/docdb/docdb_test_util.h"

#include "yb/integration-tests/cdc_test_util.h"

#include "yb/master/master_client.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"

#include "yb/util/metrics.h"
#include "yb/util/slice.h"

using std::string;

namespace yb {
namespace cdc {

using client::Flush;
using client::TransactionTestBase;
using client::WriteOpType;
using rpc::RpcController;

class CDCServiceTxnTest : public TransactionTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    mini_cluster_opt_.num_masters = 1;
    mini_cluster_opt_.num_tablet_servers = 1;
    create_table_ = false;
    SetIsolationLevel(IsolationLevel::SERIALIZABLE_ISOLATION);
    SetNumTablets(1);
    TransactionTestBase::SetUp();
    CreateTable();

    const auto mini_server = cluster_->mini_tablet_servers().front();
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(), HostPort::FromBoundEndpoint(mini_server->bound_rpc_addr()));
  }

  Status GetChangesInitialSchema(GetChangesRequestPB const& change_req,
                                 CDCCheckpointPB* mutable_checkpoint);

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
};

Status CDCServiceTxnTest::GetChangesInitialSchema(GetChangesRequestPB const& req_in,
                                                  CDCCheckpointPB* mutable_checkpoint) {
  GetChangesRequestPB change_req(req_in);
  GetChangesResponsePB change_resp;
  change_req.set_max_records(1);

  // Consume the META_OP that has the original Schema.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCHECK(!change_resp.has_error(), IllegalState,
           Format("Response Error: $0", change_resp.error().DebugString()));
    SCHECK_EQ(change_resp.records_size(), 1, IllegalState, "Expected only 1 record");
    SCHECK_EQ(change_resp.records(0).operation(), CDCRecordPB::CHANGE_METADATA,
              IllegalState, "Expected the CHANGE_METADATA related to the initial schema");
    mutable_checkpoint->CopyFrom(change_resp.checkpoint());
  }
  return Status::OK();
}

void AssertValue(const google::protobuf::Map<string, QLValuePB>& changes, int32_t expected_value) {
  ASSERT_EQ(changes.size(), 1);
  const auto& value = changes.find("value");
  ASSERT_NE(value, changes.end());
  ASSERT_EQ(value->second.int32_value(), expected_value);
}

void CheckIntentRecord(const Schema& schema, const CDCRecordPB& record, int expected_value) {
  ASSERT_EQ(record.changes_size(), 2);
  // Check the key.
  ASSERT_NO_FATALS(AssertIntKey(schema, record.key(), expected_value));
  // Make sure transaction metadata is set.
  ASSERT_TRUE(record.has_transaction_state());
  ASSERT_TRUE(record.has_time());
  const auto& transaction_state = record.transaction_state();
  ASSERT_TRUE(transaction_state.has_transaction_id());
}

void CheckApplyRecord(const CDCRecordPB& apply_record) {
  ASSERT_EQ(apply_record.changes_size(), 0);
  ASSERT_TRUE(apply_record.has_transaction_state());
  ASSERT_TRUE(apply_record.has_partition());
  const auto& txn_state = apply_record.transaction_state();
  ASSERT_TRUE(txn_state.has_transaction_id());
  ASSERT_EQ(apply_record.operation(), cdc::CDCRecordPB::APPLY);
  ASSERT_TRUE(apply_record.has_time());
}

void CheckRegularRecord(const Schema& schema, const CDCRecordPB& record, int expected_value) {
  ASSERT_EQ(record.changes_size(), 2);
  ASSERT_NO_FATALS(AssertIntKey(schema, record.key(), expected_value));
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
  docdb::DisableYcqlPackedRow();

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
  ASSERT_OK(
      client_->GetTablets(table_->name(), 0, &tablets, /* partition_list_version =*/ nullptr));
  ASSERT_EQ(tablets.size(), 1);

  std::string tablet_id = tablets.Get(0).tablet_id();
  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  auto schema =
      ASSERT_RESULT(tserver->tablet_manager()->GetTablet(tablet_id))->shared_tablet()->schema();

  auto stream_id = CreateXClusterStream(*client_, table_.table()->id());

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_stream_id(stream_id.ToString());
  change_req.set_tablet_id(tablet_id);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // Expect total 8 records: 1 META OP, 5 WRITE_OP, and 2 UPDATE_TRANSACTION_OP records.
    ASSERT_EQ(change_resp.records_size(), 8);

    struct Record {
      int32_t value; // 0 if apply record.
      bool is_intent; // Differentiate between intent and regular record.
    };

    // We expect a order of records: we will receive intents at write time and not commit time.
    Record expected_write_records_order[5] =
            {{10000, false}, {10001, true}, {10002, true}, {10003, false}, {10004, false}};
    int write_count = 0;
    int apply_count = 0;

    for (int i = 0; i < change_resp.records_size(); i++) {
      auto& record = change_resp.records(i);
      if (record.operation() == CDCRecordPB::APPLY) {
        apply_count++;
        ASSERT_NO_FATALS(CheckApplyRecord(record));
      } else if (record.operation() == CDCRecordPB::WRITE) {
        ASSERT_LT(write_count, 5);
        auto& expected_record = expected_write_records_order[write_count++];
        if (expected_record.is_intent) {
          ASSERT_NO_FATALS(CheckIntentRecord(*schema, record, expected_record.value));
        } else {
          ASSERT_NO_FATALS(CheckRegularRecord(*schema, record, expected_record.value));
        }
      } else if (record.operation() == CDCRecordPB::CHANGE_METADATA) {
        ASSERT_EQ(record.change_metadata_request().schema_version(), 0);
      } else {
        FAIL() << "Unexpected record: " << record.DebugString();
      }
    }
    ASSERT_EQ(write_count, 5);
    ASSERT_EQ(apply_count, 2);
}
}

TEST_F(CDCServiceTxnTest, TestGetChangesForPendingTransaction) {
  // If GetChanges is called in the middle of a transaction, ensure that transaction is not
  // incorrectly considered as aborted if we can't find the transaction commit record.
  // A subsequent call to GetChanges after the transaction is committed should get the
  // rows committed by transaction.

  docdb::DisableYcqlPackedRow();
  static const int32_t kNumIntentsToWrite = 3;
  static const int32_t kStartKey = 10000;
  // Get tablet ID.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(
      client_->GetTablets(table_->name(), 0, &tablets, /* partition_list_version =*/ nullptr));
  ASSERT_EQ(tablets.size(), 1);

  std::string tablet_id = tablets.Get(0).tablet_id();
  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  auto schema =
      ASSERT_RESULT(tserver->tablet_manager()->GetTablet(tablet_id))->shared_tablet()->schema();

  auto stream_id = ASSERT_RESULT(CreateXClusterStream(*client_, table_.table()->id()));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  change_req.set_stream_id(stream_id.ToString());
  change_req.set_tablet_id(tablet_id);

  // Consume the META_OP that has the initial table Schema.
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

  auto txn = CreateTransaction();
  auto session = CreateSession(txn);
  ASSERT_RESULT(WriteRow(session, kStartKey /* key */, kStartKey /* value */, WriteOpType::INSERT,
                         Flush::kTrue));
  ASSERT_RESULT(WriteRow(session, kStartKey + 1 /* key */, kStartKey + 1 /* value */,
                         WriteOpType::INSERT, Flush::kTrue));
  ASSERT_RESULT(WriteRow(session, kStartKey + 2 /* key */, kStartKey + 2 /* value */,
                         WriteOpType::INSERT, Flush::kTrue));

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // Expect 3 records for the 3 intents.
    ASSERT_EQ(change_resp.records_size(), kNumIntentsToWrite);
  }

  int32_t expected_order[kNumIntentsToWrite] = {kStartKey, kStartKey + 1, kStartKey + 2};

  for (int i = 0; i < change_resp.records_size(); i++) {
    ASSERT_NO_FATALS(CheckIntentRecord(*schema, change_resp.records(i), expected_order[i]));
  }

  // Commit transaction.
  ASSERT_OK(txn->CommitFuture().get());
  ASSERT_OK(session->TEST_Flush());

  auto checkpoint = change_resp.checkpoint();

  // Get CDC changes.
  {
    // Need to poll because Flush returns on majority_replicated and CDC waits for fully committed.
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      RpcController rpc;
      *change_req.mutable_from_checkpoint() = checkpoint;
      change_resp.Clear();
      RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
      if (change_resp.has_error()) return Result<bool>(StatusFromPB(change_resp.error().status()));
      // Expect 1 new record since we've already replicated the intents.
      return change_resp.records_size() == 1;
    }, MonoDelta::FromSeconds(30), "Wait for Transaction to be committed."));
    for (int i = 0; i < change_resp.records_size() - 1; i++) {
      ASSERT_NO_FATALS(CheckIntentRecord(*schema, change_resp.records(i), expected_order[i]));
    }
    ASSERT_NO_FATALS(CheckApplyRecord(change_resp.records(change_resp.records_size() - 1)));
  }
}

TEST_F(CDCServiceTxnTest, MetricsTest) {
  docdb::DisableYcqlPackedRow();
  static const int32_t entry_to_add = 100;
  auto txn = CreateTransaction();
  auto session = CreateSession(txn);
  ASSERT_RESULT(WriteRow(session, entry_to_add /* key */, entry_to_add /* value */,
                         WriteOpType::INSERT, Flush::kTrue));
  ASSERT_OK(txn->CommitFuture().get());

  // Get tablet ID.
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(
      client_->GetTablets(table_->name(), 0, &tablets, /* partition_list_version =*/ nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateXClusterStream(*client_, table_.table()->id()));

  auto tablet_id = tablets.Get(0).tablet_id();

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_stream_id(stream_id.ToString());
  change_req.set_tablet_id(tablet_id);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // 1 META_OP, 1 TXN, 1 WRITE
    ASSERT_EQ(change_resp.records_size(), 3);
  }

    const auto& tserver = cluster_->mini_tablet_server(0)->server();
    auto cdc_service = dynamic_cast<CDCServiceImpl*>(tserver->GetCDCService().get());
    auto metrics = ASSERT_RESULT(GetXClusterTabletMetrics(*cdc_service, tablet_id, stream_id));
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto lag = metrics->async_replication_sent_lag_micros->value();
          YB_LOG_EVERY_N_SECS(INFO, 1) << "Sent lag: " << lag << "us";
          // Only check sent lag, since we're just calling GetChanges once and expect committed lag
          // to be greater than 0.
          return lag <= 0;
        },
        MonoDelta::FromSeconds(10), "Wait for Sent Lag == 0"));
}

} // namespace cdc
} // namespace yb
