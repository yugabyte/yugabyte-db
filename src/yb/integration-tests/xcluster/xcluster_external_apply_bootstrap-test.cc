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
//

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/intent_format.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/schema_packing.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/memory/arena.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"
#include "yb/util/uuid.h"

using namespace std::chrono_literals;
using std::string;

DECLARE_bool(enable_ysql);
DECLARE_bool(ycql_enable_packed_row);
DECLARE_bool(advance_intents_flushed_op_id_to_match_regular);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_bool(enable_load_balancing);

namespace yb {

using dockv::DocKey;
using dockv::KeyBytes;
using dockv::KeyEntryValue;
using dockv::MakeKeyEntryValues;

namespace {

const client::YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "external_apply_test");

// A fixed (arbitrary) transaction id and involved-tablet uuid for the injected external write.
// Their exact values are irrelevant: the apply path only decodes them, it does not validate them
// against anything.
const char* const kTxnId = "0000000000000001";
const char* const kInvolvedTablet = "4c3e1d91-5ea7-4449-8bb3-8b0a3f9ae903";

// Test-level re-implementation of DocDBRocksDBUtil::ProcessExternalIntents (which is only available
// to docdb unit tests). Encodes a single external-transaction intent -- the (DocKey, packed-value)
// pair that the xCluster producer would have sent -- into the single key/value pair that the
// xCluster consumer stores: this is exactly what tserver::CombineExternalIntents (used by the real
// consumer in xcluster_write_implementations.cc) produces.
std::pair<string, string> EncodeExternalIntent(
    const TransactionId& txn_id, SubTransactionId subtransaction_id,
    const KeyBytes& encoded_doc_key, const string& value, const Uuid& involved_tablet) {
  class Provider : public docdb::ExternalIntentsProvider {
   public:
    Provider(KeyBytes key, string value, const Uuid& involved_tablet)
        : intent_key_(std::move(key)), intent_value_(std::move(value)),
          involved_tablet_(involved_tablet) {}

    void SetKey(const Slice& slice) override { key_.AppendRawBytes(slice); }
    void SetValue(const Slice& slice) override { value_.assign(slice.cdata(), slice.size()); }

    std::optional<std::pair<Slice, Slice>> Next() override {
      if (consumed_) {
        return std::nullopt;
      }
      consumed_ = true;
      return std::pair<Slice, Slice>(intent_key_.AsSlice(), Slice(intent_value_));
    }

    const Uuid& InvolvedTablet() override { return involved_tablet_; }

    KeyBytes key_;
    string value_;

   private:
    KeyBytes intent_key_;
    string intent_value_;
    const Uuid involved_tablet_;
    bool consumed_ = false;
  };

  Provider provider(encoded_doc_key, value, involved_tablet);
  docdb::CombineExternalIntents(txn_id, subtransaction_id, &provider);
  return {provider.key_.AsSlice().ToBuffer(), provider.value_};
}

}  // namespace

// ================================================================================================
// End-to-end (mini-cluster) regression test for GH#31899: on an xCluster *consumer*, an
// ungraceful restart re-applies an already-applied external transactional write, which -- once
// compaction has re-packed the row -- shadows the merged packed row and loses a column update.
//
// The whole chain runs through the real Write RPC -> Raft -> WAL -> compaction -> tablet
// bootstrap, exercising the bootstrap replay decision itself rather than driving the apply path
// directly.
//
// Mechanism:
//   * The xCluster output client (the consumer-side writer) fuses an external transaction's
//     intent write and its APPLY into a single WriteRequestPB / single WRITE_OP
//     (xcluster_write_implementations.cc AddRecord). When a batch contains both an external-intent
//     write_pair and an apply_external_transactions entry for the same txn,
//     NonTransactionalBatchWriter::AddEntryToWriteBatch (rocksdb_writer.cc) applies the intent
//     value *inline, straight to the regular DB* and never stores it in the intents DB (an
//     optimization: the fused form skips the intents-DB round trip entirely).
//   * That op is classified write_op_has_transaction == true (its key starts with
//     kExternalTransactionId), so tablet_bootstrap.cc ShouldReplayOperation gates it on the
//     *intents* flushed OpId, never the *regular* one. With the intents DB empty (intents_flushed
//     == 0) and the regular DB flushed past the op, the op is replayed on bootstrap even though its
//     regular effect was already durable -- re-applying the original packed row.
//   * FLAGS_advance_intents_flushed_op_id_to_match_regular = false keeps the intents flushed OpId
//     from being advanced to match the regular one on flush
//     (Tablet::MayModifyIntentsDbFlushedOpId), preserving the lag across the restart.
//
// We drive both writes as injected external writes at fixed (past) hybrid times so the packed-row
// repack is fully deterministic:
//   W1: external transactional write inserting a packed row { v = NULL } at commit_ht = 1000us.
//   W2: external single-shard per-column write setting v = "CONST" at 2000us.
//   Compaction folds W2 into the packed row and parks the merged row at W1's hybrid time (1000us).
//   After an ungraceful restart, bootstrap replays W1 -> re-applies the packed row { v = NULL } at
//   the SAME (key, hybrid_time, write_id) as the merged row, so its higher sequence number shadows
//   it and "CONST" is lost.
// ================================================================================================
class XClusterExternalApplyBootstrapTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    // Packed rows enabled so compaction performs the packed-row repack (the table is YCQL).
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_enable_packed_row) = true;
    // The crux: do not advance the intents flushed OpId to match the regular one, so the intents
    // frontier stays behind across the restart and bootstrap replays the (intents-gated) W1.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_advance_intents_flushed_op_id_to_match_regular) = false;
    // Retain no history, so the compaction cutoff is past both writes and the repack merges them.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    // Single TServer, but pin everything anyway to keep the tablet leader stable.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    client_ = ASSERT_RESULT(cluster_->CreateClient());

    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        kTableName.namespace_name(), kTableName.namespace_type()));
    client::YBSchemaBuilder builder;
    builder.AddColumn("k")->Type(DataType::STRING)->HashPrimaryKey()->NotNull();
    builder.AddColumn("v")->Type(DataType::STRING)->Nullable();
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);
    ASSERT_OK(table_.Create(kTableName, /* num_tablets= */ 1, client_.get(), &builder));
  }

  void DoTearDown() override {
    client_.reset();
    YBMiniClusterTestBase::DoTearDown();  // Shuts down and resets cluster_.
  }

  Result<tablet::TabletPeerPtr> FindLeaderPeer() {
    auto peers = VERIFY_RESULT(ListTabletPeersForTableName(
        cluster_.get(), kTableName.table_name(), ListPeersFilter::kLeaders));
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected exactly one leader peer");
    return peers.front();
  }

  // Encodes the RocksDB value bytes of the packed row { v = <v_value> } for our table's schema:
  // the payload the producer side would have sent for a full-row write.
  Result<string> EncodePackedRowValue(tablet::Tablet* tablet, const QLValuePB& v_value) {
    constexpr SchemaVersion kVersion = 0;
    auto doc_read_context = tablet->GetDocReadContext();
    const auto& schema_packing =
        VERIFY_RESULT(doc_read_context->schema_packing_storage.GetPacking(kVersion)).get();
    // V1 is the packed-row format YCQL writes produce; V2 exists only behind the YSQL-only
    // preview flag ysql_use_packed_row_v2 (default false).
    dockv::RowPackerV1 packer(
        kVersion, schema_packing, /* packed_size_limit= */ std::numeric_limits<int64_t>::max(),
        /* value_control_fields= */ Slice());
    RETURN_NOT_OK(packer.AddValue(GetValueColumnId(*tablet), v_value));
    return VERIFY_RESULT(packer.Complete()).ToBuffer();
  }

  ColumnId GetValueColumnId(tablet::Tablet& tablet) {
    return tablet.GetDocReadContext()->schema().column_id(1);  // column "v".
  }

  // Sends a pre-encoded external WriteRequestPB (write_batch + external_hybrid_time) through the
  // TServer Write RPC, so it goes through Raft -> WAL and is replayable on bootstrap.
  Status SendExternalWrite(const tserver::WriteRequestPB& req) {
    auto proxy = cluster_->mini_tablet_server(0)->server()->proxy();
    return LoggedWaitFor(
        [&req, &proxy]() -> Result<bool> {
          tserver::WriteResponsePB resp;
          rpc::RpcController rpc;
          auto s = proxy->Write(req, &resp, &rpc);
          if (s.IsTryAgain()) {
            return false;
          }
          RETURN_NOT_OK(s);
          if (resp.has_error()) {
            auto status = StatusFromPB(resp.error().status());
            if (status.IsTryAgain()) {
              return false;
            }
            return status;
          }
          return true;
        },
        MonoDelta::FromSeconds(10) * kTimeMultiplier, "external write RPC to be accepted");
  }

  // Reads the table with a full scan and returns the single row's value of column "v". A full
  // scan (rather than a point read) because the injected row carries a synthetic hash that a
  // hash-routed point read of "row1" would not compute.
  Result<QLValue> ReadSingleRowValue() {
    std::optional<QLValue> value;
    for (const auto& row : client::TableRange(table_)) {
      SCHECK(!value.has_value(), IllegalState, "Expected exactly one row");
      SCHECK_EQ(row.column(0).string_value(), "row1", IllegalState, "Unexpected row key");
      value = row.column(1);
    }
    SCHECK(value.has_value(), IllegalState, "Expected exactly one row, found none");
    return *value;
  }

  std::unique_ptr<client::YBClient> client_;
  client::TableHandle table_;
};

TEST_F(XClusterExternalApplyBootstrapTest, ExternalApplyReplayedAfterRestartShadowsPackedRow) {
  // Hybrid times for the injected external writes (all in the deep past, fully controlled).
  const auto kRowHybridTime = 1000_usec_ht;     // W1's external commit hybrid time.
  const auto kColumnHybridTime = 2000_usec_ht;  // W2's external hybrid time (> kRowHybridTime).

  auto peer = ASSERT_RESULT(FindLeaderPeer());
  const auto tablet_id = peer->tablet_id();
  auto tablet = ASSERT_RESULT(peer->shared_tablet());

  const auto txn_id = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));
  const auto involved_tablet = ASSERT_RESULT(Uuid::FromString(kInvolvedTablet));

  // Use hash 0 rather than the real hash of "row1": the single tablet covers the entire hash
  // range, so the row is in range regardless, and the reads below use a full scan, which decodes
  // rows from the stored doc keys without re-deriving the hash.
  const DocKey doc_key(0, MakeKeyEntryValues("row1"));
  const auto encoded_doc_key = doc_key.Encode();
  const auto value_column_id = GetValueColumnId(*tablet);

  QLValuePB const_val;
  const_val.set_string_value("CONST");
  QLValuePB null_val;  // An unset QLValuePB encodes NULL.

  // ----------------------------------------------------------------------------------------------
  // W1: external transactional write inserting the packed row { v = NULL } at
  // commit_ht = kRowHybridTime. The intent write_pair and the APPLY are fused into one
  // WriteRequestPB, so the apply fires inline to the regular DB and the op is classified
  // transactional (intents-gated).
  // ----------------------------------------------------------------------------------------------
  {
    const auto packed_null = ASSERT_RESULT(EncodePackedRowValue(tablet.get(), null_val));
    auto [intent_key, intent_value] = EncodeExternalIntent(
        txn_id, kMinSubTransactionId, encoded_doc_key, packed_null, involved_tablet);

    tserver::WriteRequestPB req;
    req.set_tablet_id(tablet_id);
    req.set_external_hybrid_time(kRowHybridTime.ToUint64());
    auto* batch = req.mutable_write_batch();
    auto* write_pair = batch->add_write_pairs();
    write_pair->set_key(intent_key);
    write_pair->set_value(intent_value);

    auto* apply = batch->add_apply_external_transactions();
    apply->set_transaction_id(txn_id.AsSlice().cdata(), txn_id.AsSlice().size());
    apply->set_commit_hybrid_time(kRowHybridTime.ToUint64());
    apply->set_filter_range_encoded(true);  // empty start/end keys -> apply everything.

    ASSERT_OK(SendExternalWrite(req));
  }

  // ----------------------------------------------------------------------------------------------
  // W2: external single-shard per-column write setting v = "CONST" at kColumnHybridTime
  // (> kRowHybridTime). Lands as a column delta at (DocKey, ColumnId(v)) @ kColumnHybridTime;
  // classified non-transactional (regular-gated), so it is NOT replayed on bootstrap.
  // ----------------------------------------------------------------------------------------------
  {
    KeyBytes column_key(encoded_doc_key);
    KeyEntryValue::MakeColumnId(value_column_id).AppendToKey(&column_key);
    string column_value;
    dockv::AppendEncodedValue(const_val, &column_value);

    tserver::WriteRequestPB req;
    req.set_tablet_id(tablet_id);
    req.set_external_hybrid_time(kColumnHybridTime.ToUint64());
    auto* write_pair = req.mutable_write_batch()->add_write_pairs();
    write_pair->set_key(column_key.AsSlice().cdata(), column_key.AsSlice().size());
    write_pair->set_value(column_value);

    ASSERT_OK(SendExternalWrite(req));
  }

  // Flush the regular DB (the intents DB is empty -> stays unflushed -> intents_flushed == 0) and
  // compact, so the packed-row repack folds W2 into W1's packed row and parks the merged row at
  // W1's hybrid time. After this, the regular flushed OpId is past W1.
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kFalse));

  auto dump_before = ASSERT_RESULT(DumpTableLeadersDocDB(cluster_.get(), kTableName.table_name()));
  LOG(INFO) << "DocDB before restart:\n" << dump_before;

  // The repack must leave the merged packed row as the tablet's single regular-DB record (the
  // standalone W2 delta is folded in and dropped). This is the precondition that makes the test
  // meaningful: without the fold there is nothing for a replayed W1 to shadow, and the test would
  // pass vacuously even with broken replay gating. Same record-count idiom as
  // packed_row_test_base's CheckNumRecords.
  ASSERT_EQ(ASSERT_RESULT(tablet->TEST_CountDBRecords(docdb::StorageDbType::kRegular)), 1)
      << "Expected the repack to fold W2's delta into the packed row; see the DocDB dump above.";

  // Sanity: the row reads v = "CONST" before the restart.
  ASSERT_EQ(ASSERT_RESULT(ReadSingleRowValue()).string_value(), "CONST");

  // Confirm the frontier setup that drives the bug: intents flushed OpId lags W1, regular does not.
  {
    auto flushed = ASSERT_RESULT(tablet->MaxPersistentOpId(/* invalid_if_no_new_data= */ false));
    LOG(INFO) << "Before restart, flushed OpIds: regular=" << flushed.regular
              << " intents=" << flushed.intents;
    ASSERT_LT(flushed.intents.index, flushed.regular.index)
        << "Test setup invalid: intents must lag regular for bootstrap to replay the external "
        << "apply.";
  }
  tablet.reset();

  // Ungraceful restart -> local bootstrap replays the WAL.
  ASSERT_OK(cluster_->RestartSync());
  ASSERT_OK(WaitFor(
      [this, &tablet_id]() -> Result<bool> {
        auto peer = cluster_->mini_tablet_server(0)->server()->tablet_manager()->LookupTablet(
            tablet_id);
        return peer && peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
      },
      MonoDelta::FromSeconds(60) * kTimeMultiplier, "tablet leader ready after restart"));

  // Collapse the two same-key versions: the re-applied { v = NULL } has the higher sequence number,
  // so compaction keeps it and the merged { v = "CONST" } is gone.
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kFalse));

  // The row must still read v = "CONST". Without per-storage replay gating, bootstrap re-applies
  // W1's original packed row, which shadows the merged row and loses the column update (the row
  // reads NULL) -- the GH#31899 corruption.
  auto dump_after = ASSERT_RESULT(DumpTableLeadersDocDB(cluster_.get(), kTableName.table_name()));
  LOG(INFO) << "DocDB after restart:\n" << dump_after;
  const auto value_after = ASSERT_RESULT(ReadSingleRowValue());
  ASSERT_FALSE(value_after.IsNull())
      << "Column update lost: v reads NULL after the bootstrap replay re-applied W1.";
  ASSERT_EQ(value_after.string_value(), "CONST");
}

}  // namespace yb
