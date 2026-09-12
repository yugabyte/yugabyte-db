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

#include "yb/common/ql_type.h"

#include "yb/common/transaction.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb-test.h"

#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/rocksdb_writer.h"
#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/partition.h"

#include "yb/vector_index/vector_index_fwd.h"

namespace yb::docdb {

static const char* kTabletUUID = "4c3e1d91-5ea7-4449-8bb3-8b0a3f9ae903";
static const char* kTxnId = "0000000000000001";

// Minimal DocVectorIndex test double for the external-apply vector-feed gating test (GH#31899).
// Only the methods the external apply touches are functional -- indexed_table_key_prefix(),
// column_id(), hybrid_time(), and Insert() (which records how many entries were fed). Everything
// else is unreachable on this path and fatal if called.
class CountingVectorIndex : public DocVectorIndex {
 public:
  CountingVectorIndex(Slice table_key_prefix, ColumnId column_id)
      : table_key_prefix_(table_key_prefix.ToBuffer()), column_id_(column_id) {}

  Slice indexed_table_key_prefix() const override { return table_key_prefix_; }
  ColumnId column_id() const override { return column_id_; }
  HybridTime hybrid_time() const override { return HybridTime::kMin; }
  Status Insert(
      const DocVectorIndexInsertEntries& entries, const InsertOptions& options) override {
    inserted_entries_ += entries.size();
    return Status::OK();
  }
  size_t inserted_entries() const { return inserted_entries_; }

  // Unused on the external-apply vector-feed path.
  const TableId& table_id() const override { LOG(FATAL) << "Unexpected call"; }
  const PgVectorIdxOptionsPB& options() const override { LOG(FATAL) << "Unexpected call"; }
  const std::string& path() const override { LOG(FATAL) << "Unexpected call"; }
  const DocVectorIndexContext& context() const override { LOG(FATAL) << "Unexpected call"; }
  const DocVectorIndexMetrics& metrics() const override { LOG(FATAL) << "Unexpected call"; }
  size_t EstimateNumVectorsForBytes(size_t) const override { LOG(FATAL) << "Unexpected call"; }
  Result<DocVectorIndexSearchResult> Search(
      Slice, const vector_index::SearchOptions&, bool, DocVectorIndexReverseMappingReader&)
      override {
    LOG(FATAL) << "Unexpected call";
  }
  Result<EncodedDistance> Distance(Slice, Slice) override { LOG(FATAL) << "Unexpected call"; }
  void EnableAutoCompactions() override { LOG(FATAL) << "Unexpected call"; }
  Status Compact() override { LOG(FATAL) << "Unexpected call"; }
  Status WaitForCompaction() override { LOG(FATAL) << "Unexpected call"; }
  Status Flush() override { LOG(FATAL) << "Unexpected call"; }
  Status WaitForFlush() override { LOG(FATAL) << "Unexpected call"; }
  storage::FrontierInfo GetFrontiers(storage::FrontierKinds) override {
    LOG(FATAL) << "Unexpected call";
  }
  storage::FlushAbility GetFlushAbility() override { LOG(FATAL) << "Unexpected call"; }
  Status CreateCheckpoint(const std::string&) override { LOG(FATAL) << "Unexpected call"; }
  const std::string& ToString() const override { LOG(FATAL) << "Unexpected call"; }
  Result<bool> HasVectorId(const vector_index::VectorId&) const override {
    LOG(FATAL) << "Unexpected call";
  }
  Status Destroy() override { LOG(FATAL) << "Unexpected call"; }
  Result<size_t> TotalEntries() const override { LOG(FATAL) << "Unexpected call"; }
  uint64_t OnDiskSize() const override { LOG(FATAL) << "Unexpected call"; }
  void StartShutdown() override { LOG(FATAL) << "Unexpected call"; }
  void CompleteShutdown() override { LOG(FATAL) << "Unexpected call"; }
  bool TEST_HasBackgroundInserts() const override { LOG(FATAL) << "Unexpected call"; }
  size_t TEST_NextManifestFileNo() const override { LOG(FATAL) << "Unexpected call"; }

 private:
  const std::string table_key_prefix_;
  const ColumnId column_id_;
  size_t inserted_entries_ = 0;
};

class NonTransactionalBatchWriterTest : public DocDBTestBase {
 public:
  void SetUp() override {
    DocDBTestBase::SetUp();

    // Needed to ensure that intents are ordered deterministically.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_sort_weak_intents) = true;
  }

  Schema CreateSchema() override { return Schema(); }

  Status SendWriteBatch(
      const docdb::LWKeyValueWriteBatchPB& put_batch, HybridTime write_ht, HybridTime batch_ht,
      const DocVectorIndexesPtr& vector_indexes = nullptr,
      const StorageSet& apply_to_storages = StorageSet::All(),
      TableType table_type = TableType::PGSQL_TABLE_TYPE,
      std::atomic<bool>* can_advance_intents_flush_op_id = nullptr,
      std::optional<IntraTxnWriteId> write_id_override = std::nullopt) {
    ConsensusFrontiers frontiers;
    rocksdb::WriteBatch intents_write_batch;
    NonTransactionalBatchWriter batcher(
        put_batch, write_ht, batch_ht, intents_db(), &intents_write_batch, *this, frontiers,
        vector_indexes, apply_to_storages, table_type, can_advance_intents_flush_op_id,
        write_id_override);

    rocksdb::WriteBatch regular_write_batch;
    regular_write_batch.SetFrontiers(&frontiers);
    regular_write_batch.SetDirectWriter(&batcher);
    RETURN_NOT_OK(regular_db_->Write(write_options(), &regular_write_batch));

    if (intents_write_batch.Count() != 0) {
      RETURN_NOT_OK(intents_db_->Write(write_options(), &intents_write_batch));
    }

    return Status::OK();
  }

  // Runs a foreground transactional intent batch through TransactionalWriter with a seeded
  // intra-transaction write ID counter, exactly like Tablet::WriteTransactionalBatch seeds it
  // from the participant's next_write_id. Applies through a no-op handler rather than a real
  // RocksDB write: a DirectWriter failure inside a DB write is cached as a background error
  // that fails the fixture's DB destructor at teardown.
  Status ApplyTransactionalBatch(
      const docdb::LWKeyValueWriteBatchPB& put_batch, HybridTime hybrid_time,
      const TransactionId& txn_id, IntraTxnWriteId start_write_id) {
    class NoopDirectWriteHandler : public rocksdb::DirectWriteHandler {
     public:
      std::pair<Slice, Slice> Put(
          const rocksdb::SliceParts& /* key */, const rocksdb::SliceParts& /* value */) override {
        return {};
      }
      void SingleDelete(const Slice& /* key */) override {}
    };

    TransactionalWriter writer(
        put_batch, hybrid_time, txn_id, IsolationLevel::SNAPSHOT_ISOLATION,
        dockv::PartialRangeKeyIntents::kTrue, /* replicated_batches_state= */ Slice(),
        start_write_id, /* applier= */ nullptr);
    NoopDirectWriteHandler handler;
    return writer.Apply(handler);
  }

  std::string GetEncodedHashPartitionKey(uint16_t hash) {
    dockv::KeyBytes encoded_key;
    dockv::DocKeyEncoderAfterTableIdStep(&encoded_key).Hash(
        hash, dockv::KeyEntryValues());
    return encoded_key.ToStringBuffer();
  }

  std::string GetEncodedHashPartitionKey(const std::string& key) {
    KeyBytes encoded_key;
    KeyEntryValue(key).AppendToKey(&encoded_key);

    return encoded_key.ToStringBuffer();
  }

  void AddApplyExternalTxn(
      docdb::LWKeyValueWriteBatchPB* put_batch, const TransactionId& txn_id, HybridTime commit_ht,
      const Slice& filter_start_key = "", const Slice& filter_end_key = "") {
    auto* apply_txn = put_batch->add_apply_external_transactions();
    apply_txn->dup_transaction_id(txn_id.AsSlice());
    apply_txn->set_commit_hybrid_time(commit_ht.ToUint64());
    apply_txn->dup_filter_start_key(filter_start_key);
    apply_txn->dup_filter_end_key(filter_end_key);
    apply_txn->set_filter_range_encoded(true);
  }

  void AddExternalIntentsWritePair(
      docdb::LWKeyValueWriteBatchPB* put_batch, const TransactionId& txn_id,
      SubTransactionId subtransaction_id, const std::vector<ExternalIntent>& intents,
      const Uuid& involved_tablet) {
    auto* write_pair = put_batch->add_write_pairs();
    auto [key, value] = ProcessExternalIntents(txn_id, subtransaction_id, intents, involved_tablet);
    write_pair->dup_key(key.AsSlice());
    write_pair->dup_value(value.AsSlice());
  }

 protected:
  ThreadSafeArena arena_;
};

TEST_F(NonTransactionalBatchWriterTest, SeparateBatchesAtSameFixedHybridTime) {
  const DocKey doc_key(0, MakeKeyEntryValues("row"));
  const auto encoded_key = doc_key.Encode();
  const auto kWriteHT = 6000_usec_ht;

  docdb::LWKeyValueWriteBatchPB first_batch(&arena_);
  auto* first_pair = first_batch.add_write_pairs();
  first_pair->dup_key(encoded_key.AsSlice());
  first_pair->dup_value(EncodeValue(QLValue::Primitive("first")));
  ASSERT_OK(SendWriteBatch(first_batch, kWriteHT, 7000_usec_ht));

  docdb::LWKeyValueWriteBatchPB second_batch(&arena_);
  auto* second_pair = second_batch.add_write_pairs();
  second_pair->dup_key(encoded_key.AsSlice());
  second_pair->dup_value(EncodeValue(QLValue::Primitive("second")));
  ASSERT_OK(SendWriteBatch(second_batch, kWriteHT, 8000_usec_ht));

  // Each batch starts at write ID zero, so both writes produce the same physical RocksDB key.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["row"], []), [HT{ physical: 6000 }]) -> "second"
    )#");
}

TEST_F(
    NonTransactionalBatchWriterTest,
    SeparateBatchesAtSameFixedHybridTimeWithWriteIdOverride) {
  const DocKey doc_key(0, MakeKeyEntryValues("row"));
  const auto encoded_key = doc_key.Encode();
  const auto kWriteHT = 6000_usec_ht;

  docdb::LWKeyValueWriteBatchPB first_batch(&arena_);
  auto* first_pair = first_batch.add_write_pairs();
  first_pair->dup_key(encoded_key.AsSlice());
  first_pair->dup_value(EncodeValue(QLValue::Primitive("first")));
  ASSERT_OK(SendWriteBatch(
      first_batch, kWriteHT, 7000_usec_ht, nullptr, StorageSet::All(),
      TableType::PGSQL_TABLE_TYPE, /* can_advance_intents_flush_op_id= */ nullptr, 100));

  docdb::LWKeyValueWriteBatchPB second_batch(&arena_);
  auto* second_pair = second_batch.add_write_pairs();
  second_pair->dup_key(encoded_key.AsSlice());
  second_pair->dup_value(EncodeValue(QLValue::Primitive("second")));
  ASSERT_OK(SendWriteBatch(
      second_batch, kWriteHT, 8000_usec_ht, nullptr, StorageSet::All(),
      TableType::PGSQL_TABLE_TYPE, /* can_advance_intents_flush_op_id= */ nullptr, 101));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["row"], []), [HT{ physical: 6000 w: 101 }]) -> "second"
SubDocKey(DocKey(0x0000, ["row"], []), [HT{ physical: 6000 w: 100 }]) -> "first"
    )#");

  // A cutoff strictly before the fixed write time preserves every candidate through compaction.
  FullyCompactHistoryBefore(kWriteHT.Decremented());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["row"], []), [HT{ physical: 6000 w: 101 }]) -> "second"
SubDocKey(DocKey(0x0000, ["row"], []), [HT{ physical: 6000 w: 100 }]) -> "first"
    )#");

  // Equality is insufficient: the cutoff uses kMaxWriteId and retains only the newest write ID.
  FullyCompactHistoryBefore(kWriteHT);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["row"], []), [HT{ physical: 6000 w: 101 }]) -> "second"
    )#");
}

TEST_F(
    NonTransactionalBatchWriterTest,
    MarkedAndUnmarkedWritesWithSameHybridTimeAndWriteIdCollide) {
  const auto kWriteHT = 6000_usec_ht;
  const auto unmarked_first_key = DocKey(0, MakeKeyEntryValues("unmarked-first")).Encode();
  const auto marked_first_key = DocKey(0, MakeKeyEntryValues("marked-first")).Encode();

  auto write = [&](const auto& encoded_key, const char* value, HybridTime batch_ht,
                   std::optional<IntraTxnWriteId> write_id_override) -> Status {
    docdb::LWKeyValueWriteBatchPB batch(&arena_);
    auto* write_pair = batch.add_write_pairs();
    write_pair->dup_key(encoded_key.AsSlice());
    write_pair->dup_value(EncodeValue(QLValue::Primitive(value)));
    return SendWriteBatch(
        batch, kWriteHT, batch_ht, nullptr, StorageSet::All(), TableType::PGSQL_TABLE_TYPE,
        /* can_advance_intents_flush_op_id= */ nullptr, write_id_override);
  };

  ASSERT_OK(write(
      unmarked_first_key, "foreground-first", 7000_usec_ht, /* write_id_override= */ std::nullopt));
  ASSERT_OK(write(
      unmarked_first_key, "backfill-last", 8000_usec_ht, /* write_id_override= */ 0));
  ASSERT_OK(write(
      marked_first_key, "backfill-first", 9000_usec_ht, /* write_id_override= */ 0));
  ASSERT_OK(write(
      marked_first_key, "foreground-last", 10000_usec_ht,
      /* write_id_override= */ std::nullopt));

  // Both paths use write ID zero, so application order alone determines which value survives.
  // This is the raw collision the production write-ID floor exists to prevent: marked writes
  // derive (kBackfillWriteIdFloor | raft_index) at the tablet layer, so they can never share a
  // write ID with an unmarked write. See FloorSeparatesMarkedAndUnmarkedWriteIdDomains.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["marked-first"], []), [HT{ physical: 6000 }]) -> "foreground-last"
SubDocKey(DocKey(0x0000, ["unmarked-first"], []), [HT{ physical: 6000 }]) -> "backfill-last"
    )#");
}

TEST_F(NonTransactionalBatchWriterTest, FloorSeparatesMarkedAndUnmarkedWriteIdDomains) {
  // With the write-ID floor applied to marked writes (as Tablet::ApplyOperation derives
  // kBackfillWriteIdFloor | raft_index), a marked and an unmarked write to the same key at the
  // same fixed hybrid time land as distinct physical versions in either application order --
  // the collapse demonstrated by MarkedAndUnmarkedWritesWithSameHybridTimeAndWriteIdCollide
  // cannot occur across domains.
  const auto kWriteHT = 6000_usec_ht;
  const auto kMarkedWriteId = kBackfillWriteIdFloor | 2;
  const auto unmarked_first_key = DocKey(0, MakeKeyEntryValues("unmarked-first")).Encode();
  const auto marked_first_key = DocKey(0, MakeKeyEntryValues("marked-first")).Encode();

  auto write = [&](const auto& encoded_key, const char* value, HybridTime batch_ht,
                   std::optional<IntraTxnWriteId> write_id_override) -> Status {
    docdb::LWKeyValueWriteBatchPB batch(&arena_);
    auto* write_pair = batch.add_write_pairs();
    write_pair->dup_key(encoded_key.AsSlice());
    write_pair->dup_value(EncodeValue(QLValue::Primitive(value)));
    return SendWriteBatch(
        batch, kWriteHT, batch_ht, nullptr, StorageSet::All(), TableType::PGSQL_TABLE_TYPE,
        /* can_advance_intents_flush_op_id= */ nullptr, write_id_override);
  };

  ASSERT_OK(write(
      unmarked_first_key, "foreground", 7000_usec_ht, /* write_id_override= */ std::nullopt));
  ASSERT_OK(write(unmarked_first_key, "backfill", 8000_usec_ht, kMarkedWriteId));
  ASSERT_OK(write(marked_first_key, "backfill", 9000_usec_ht, kMarkedWriteId));
  ASSERT_OK(write(
      marked_first_key, "foreground", 10000_usec_ht, /* write_id_override= */ std::nullopt));

  // Every version survives; the floored (marked) version sorts newer within the key.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
SubDocKey(DocKey(0x0000, ["marked-first"], []), [HT{ physical: 6000 w: $0 }]) -> "backfill"
SubDocKey(DocKey(0x0000, ["marked-first"], []), [HT{ physical: 6000 }]) -> "foreground"
SubDocKey(DocKey(0x0000, ["unmarked-first"], []), [HT{ physical: 6000 w: $0 }]) -> "backfill"
SubDocKey(DocKey(0x0000, ["unmarked-first"], []), [HT{ physical: 6000 }]) -> "foreground"
    )#", kMarkedWriteId));
}

TEST_F(NonTransactionalBatchWriterTest, ForegroundIntraTxnWriteIdCapRejectsAtLimit) {
  // The always-on foreground cap: a transaction may never write an intent with an
  // intra-transaction write ID at or above kIntraTxnWriteIdLimit -- that range is reserved.
  // Without the cap the counter would cross into the reserved domain (and previously wrapped
  // silently at 2^32).
  const auto kWriteHT = 6000_usec_ht;
  const auto txn_id = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  docdb::LWKeyValueWriteBatchPB batch(&arena_);
  for (const auto* key : {"row1", "row2"}) {
    auto* write_pair = batch.add_write_pairs();
    write_pair->dup_key(DocKey(0, MakeKeyEntryValues(key)).Encode().AsSlice());
    write_pair->dup_value(EncodeValue(QLValue::Primitive("value")));
  }

  // Seeded fully below the limit, the batch is accepted.
  ASSERT_OK(ApplyTransactionalBatch(batch, kWriteHT, txn_id, /* start_write_id= */ 0));

  // The first pair consumes the last legal foreground write ID; the second must be rejected
  // instead of entering the reserved marked domain.
  const auto status = ApplyTransactionalBatch(
      batch, kWriteHT, txn_id, /* start_write_id= */ kIntraTxnWriteIdLimit - 1);
  ASSERT_TRUE(status.IsIllegalState()) << status;
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "intra-transaction write ID limit");
}

TEST_F(NonTransactionalBatchWriterTest, SimpleTransaction) {
  // Simple test where we write two batches of external intents, then apply them.
  // Ensure that we external intents are cleaned up after applying and that regulardb entries have
  // the proper write_ids.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);

  const DocKey hk1(0, MakeKeyEntryValues("h1"));
  const DocKey hk2(1, MakeKeyEntryValues("h2"));
  const auto encoded_hk1 = hk1.Encode();
  const auto encoded_hk2 = hk2.Encode();
  Uuid involved_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  TransactionId txn1 = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  // Construct two batches of external intents.
  std::vector<ExternalIntent> intents = {
      {DocPath(encoded_hk1), EncodeValue(QLValue::Primitive("value1"))},
      {DocPath(encoded_hk2), EncodeValue(QLValue::Primitive("value2"))}};
  AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, involved_tablet);

  // Second batch should end up overwriting the previous batch.
  intents = {
      {DocPath(encoded_hk1), EncodeValue(QLValue::Primitive("value3"))},
      {DocPath(encoded_hk2), EncodeValue(QLValue::Primitive("value4"))}};
  AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, involved_tablet);

  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;
  // Write to intentsdb.
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Ensure that second batch has the correct write_id, as both batches share the same timestamp.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0000, ["h1"], []), []) -> "value1", \
    SubDocKey(DocKey(0x0001, ["h2"], []), []) -> "value2"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0000, ["h1"], []), []) -> "value3", \
    SubDocKey(DocKey(0x0001, ["h2"], []), []) -> "value4"]
    )#");

  // Send the apply transaction.
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT);
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Ensure intents are cleaned up and write_ids are correct.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value3"
SubDocKey(DocKey(0x0000, ["h1"], []), [HT{ physical: 6000 }]) -> "value1"
SubDocKey(DocKey(0x0001, ["h2"], []), [HT{ physical: 6000 w: 3 }]) -> "value4"
SubDocKey(DocKey(0x0001, ["h2"], []), [HT{ physical: 6000 w: 1 }]) -> "value2"
    )#");
}

TEST_F(NonTransactionalBatchWriterTest, ApplyFilterOnHashExternalIntents) {
  // Test using the filter_start_key and filter_end_key of ApplyExternalTransactionPB on hashed
  // keys. Ensure that only the correct intents are applied.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
  Uuid hash_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  TransactionId txn1 = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  const int kNumKeys = 10;
  vector<KeyBytes> encoded_hash_keys;
  // Mimic xCluster and group each key in separate batches.
  for (int i = 0; i < kNumKeys; ++i) {
    const DocKey hk(i * 100, MakeKeyEntryValues(Format("h$0", i)));
    std::vector<ExternalIntent> intents = {
        {DocPath(hk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i)))},
        {DocPath(hk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i + 1)))}};
    AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, hash_tablet);
  }

  // Write intents.
  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0000, ["h0"], []), []) -> "value0", \
    SubDocKey(DocKey(0x0000, ["h0"], []), []) -> "value1"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0064, ["h1"], []), []) -> "value2", \
    SubDocKey(DocKey(0x0064, ["h1"], []), []) -> "value3"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 2 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x00c8, ["h2"], []), []) -> "value4", \
    SubDocKey(DocKey(0x00c8, ["h2"], []), []) -> "value5"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 3 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x012c, ["h3"], []), []) -> "value6", \
    SubDocKey(DocKey(0x012c, ["h3"], []), []) -> "value7"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value8", \
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value10", \
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value12", \
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value14", \
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value16", \
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value18", \
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value19"]
    )#");

  // Apply with a filter, [0,400).
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, "", GetEncodedHashPartitionKey(400));
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Should apply the first 4 rows, and keep other the intents.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 6 }]) -> "value6"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value8", \
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value10", \
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value12", \
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value14", \
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value16", \
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value18", \
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value19"]
    )#");

  // Apply with a filter, [700, "").
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, GetEncodedHashPartitionKey(700), "");
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 4 }]) -> "value18"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value8", \
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value10", \
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value12", \
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value13"]
    )#");

  // Apply remaining section. There should be no intents remaining in the end.
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, GetEncodedHashPartitionKey(400),
      GetEncodedHashPartitionKey(700));
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey(0x0190, ["h4"], []), [HT{ physical: 6000 w: 1 }]) -> "value9"
SubDocKey(DocKey(0x0190, ["h4"], []), [HT{ physical: 6000 }]) -> "value8"
SubDocKey(DocKey(0x01f4, ["h5"], []), [HT{ physical: 6000 w: 3 }]) -> "value11"
SubDocKey(DocKey(0x01f4, ["h5"], []), [HT{ physical: 6000 w: 2 }]) -> "value10"
SubDocKey(DocKey(0x0258, ["h6"], []), [HT{ physical: 6000 w: 5 }]) -> "value13"
SubDocKey(DocKey(0x0258, ["h6"], []), [HT{ physical: 6000 w: 4 }]) -> "value12"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 4 }]) -> "value18"
    )#");
}

TEST_F(NonTransactionalBatchWriterTest, ApplyFilterOnRangedExternalIntents) {
  // Test using the filter_start_key and filter_end_key of ApplyExternalTransactionPB on ranged
  // keys. Ensure that only the correct intents are applied.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
  Uuid range_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  TransactionId txn1 = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  const int kNumKeys = 10;
  // Mimic xCluster and group each key in separate batches.
  for (int i = 0; i < kNumKeys; ++i) {
    const DocKey rk(MakeKeyEntryValues(Format("r$0", i)));
    std::vector<ExternalIntent> intents = {
        {DocPath(rk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i)))},
        {DocPath(rk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i + 1)))}};
    AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, range_tablet);
  }

  // Write intents.
  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r0"]), []) -> "value0", \
    SubDocKey(DocKey([], ["r0"]), []) -> "value1"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r1"]), []) -> "value2", \
    SubDocKey(DocKey([], ["r1"]), []) -> "value3"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 2 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r2"]), []) -> "value4", \
    SubDocKey(DocKey([], ["r2"]), []) -> "value5"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 3 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r3"]), []) -> "value6", \
    SubDocKey(DocKey([], ["r3"]), []) -> "value7"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r4"]), []) -> "value8", \
    SubDocKey(DocKey([], ["r4"]), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r5"]), []) -> "value10", \
    SubDocKey(DocKey([], ["r5"]), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r6"]), []) -> "value12", \
    SubDocKey(DocKey([], ["r6"]), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r7"]), []) -> "value14", \
    SubDocKey(DocKey([], ["r7"]), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r8"]), []) -> "value16", \
    SubDocKey(DocKey([], ["r8"]), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r9"]), []) -> "value18", \
    SubDocKey(DocKey([], ["r9"]), []) -> "value19"]
    )#");

  // Apply with a filter, [0,r4).
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT, "", GetEncodedHashPartitionKey("r4"));
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Should apply the first 4 rows, and keep the other intents.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 6 }]) -> "value6"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r4"]), []) -> "value8", \
    SubDocKey(DocKey([], ["r4"]), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r5"]), []) -> "value10", \
    SubDocKey(DocKey([], ["r5"]), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r6"]), []) -> "value12", \
    SubDocKey(DocKey([], ["r6"]), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r7"]), []) -> "value14", \
    SubDocKey(DocKey([], ["r7"]), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r8"]), []) -> "value16", \
    SubDocKey(DocKey([], ["r8"]), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r9"]), []) -> "value18", \
    SubDocKey(DocKey([], ["r9"]), []) -> "value19"]
    )#");

  // Apply with a filter, [r7, "").
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT, GetEncodedHashPartitionKey("r7"), "");
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 4 }]) -> "value18"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r4"]), []) -> "value8", \
    SubDocKey(DocKey([], ["r4"]), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r5"]), []) -> "value10", \
    SubDocKey(DocKey([], ["r5"]), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r6"]), []) -> "value12", \
    SubDocKey(DocKey([], ["r6"]), []) -> "value13"]
    )#");

  // Apply remaining section. All intents should be cleaned up.
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, GetEncodedHashPartitionKey("r4"),
      GetEncodedHashPartitionKey("r7"));
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey([], ["r4"]), [HT{ physical: 6000 w: 1 }]) -> "value9"
SubDocKey(DocKey([], ["r4"]), [HT{ physical: 6000 }]) -> "value8"
SubDocKey(DocKey([], ["r5"]), [HT{ physical: 6000 w: 3 }]) -> "value11"
SubDocKey(DocKey([], ["r5"]), [HT{ physical: 6000 w: 2 }]) -> "value10"
SubDocKey(DocKey([], ["r6"]), [HT{ physical: 6000 w: 5 }]) -> "value13"
SubDocKey(DocKey([], ["r6"]), [HT{ physical: 6000 w: 4 }]) -> "value12"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 4 }]) -> "value18"
    )#");
}

// GH#31899: the fused external apply honors the vector-index bit of apply_to_storages.
// On bootstrap replay, a vector index already durably flushed past the op has its bit cleared and
// must NOT be re-fed; an index whose bit is set (online apply, or a lagging index on replay) is fed
// as before. This drives NonTransactionalBatchWriter directly with the StorageSet, so it exercises
// the gating deterministically -- no cluster, restart, or intents-lag timing involved.
TEST_F(NonTransactionalBatchWriterTest, ExternalApplyGatesVectorIndexFeed) {
  // Applies one column-keyed external write (column_id 11) to a vector-indexed table with the given
  // StorageSet, and returns how many entries the external apply fed into the vector index.
  auto fed_entries = [&](const StorageSet& apply_to_storages, const char* txn_hex,
                         uint16_t hash) -> size_t {
    auto index = std::make_shared<CountingVectorIndex>(/*table_key_prefix=*/Slice(), ColumnId(11));
    auto indexes = std::make_shared<DocVectorIndexes>();
    indexes->push_back(index);

    Uuid involved_tablet = CHECK_RESULT(Uuid::FromString(kTabletUUID));
    TransactionId txn = CHECK_RESULT(FullyDecodeTransactionId(txn_hex));
    const DocKey hk(hash, MakeKeyEntryValues("h1"));
    auto column_path = DocPath(hk.Encode(), KeyEntryValue::MakeColumnId(ColumnId(11)));

    const auto kBatchHT = 5000_usec_ht;
    const auto kWriteHT = 6000_usec_ht;

    // Write the external intent (a single column value)...
    docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
    std::vector<ExternalIntent> intents = {
        {column_path, EncodeValue(QLValue::Primitive("vec"))}};
    AddExternalIntentsWritePair(&put_batch, txn, kMinSubTransactionId, intents, involved_tablet);
    CHECK_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

    // ...then apply it, passing the vector index and the StorageSet under test.
    put_batch.Clear();
    AddApplyExternalTxn(&put_batch, txn, kWriteHT);
    CHECK_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT, indexes, apply_to_storages));
    return index->inserted_entries();
  };

  // Vector-index bit clear (already flushed past this op) -> the apply must skip the vector feed.
  // The regular bit is left clear too, so the apply doesn't also write the regular-DB reverse
  // mapping (which would require a real encoded vector value); this isolates the vector-feed gate.
  StorageSet vector_flushed;
  EXPECT_EQ(fed_entries(vector_flushed, "0000000000000001", 0), 0)
      << "External apply re-fed a vector index whose bit was clear (already durably flushed).";

  // Vector-index bit set (lagging index) -> the apply feeds the vector index.
  StorageSet vector_lagging;
  vector_lagging.SetVectorIndex(0);
  EXPECT_EQ(fed_entries(vector_lagging, "0000000000000002", 100), 1)
      << "External apply did not feed a vector index whose bit was set.";
}

namespace {

std::string EncodeTableOwnedVectorColumnValue(
    const vector_index::VectorId& id, Slice vector_binary_value = {}) {
  LWQLValuePB ql_value(nullptr);
  ql_value.ref_binary_value(vector_binary_value);
  dockv::DocVectorValue doc_vector_value(dockv::VectorValueFormat::kTyped, ql_value, id);
  std::string out;
  doc_vector_value.EncodeTo(&out);
  return out;
}

KeyBytes EncodeDocPathKey(const DocPath& doc_path) {
  KeyBytes encoded_key(doc_path.encoded_doc_key().AsSlice());
  for (size_t i = 0; i < doc_path.num_subkeys(); ++i) {
    doc_path.subkey(i).AppendToKey(&encoded_key);
  }
  return encoded_key;
}

}  // namespace

// GH#32310: YSQL single-shard fast path applies via NonTransactionalBatchWriter without intents.
// Table-owned reverse mapping must be written for column-keyed vector values, and delete_vector_ids
// must tombstone obsolete vector ids.
TEST_F(NonTransactionalBatchWriterTest, FastPathVectorReverseMapping) {
  const auto vector_id = ASSERT_RESULT(vector_index::VectorIdFromString(
      "10000000-2000-3000-4000-000000000001"));

  const DocKey doc_key(MakeKeyEntryValues("row1"));
  const auto column_path = DocPath(doc_key.Encode(), KeyEntryValue::MakeColumnId(ColumnId(11)));
  const auto encoded_key = EncodeDocPathKey(column_path);
  const auto encoded_value = EncodeTableOwnedVectorColumnValue(vector_id);

  const auto kWriteHT = 6000_usec_ht;
  const auto kBatchHT = 5000_usec_ht;

  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
  auto* write_pair = put_batch.add_write_pairs();
  write_pair->dup_key(encoded_key.AsSlice());
  write_pair->dup_value(encoded_value);
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
MetaKey(VectorId(10000000-2000-3000-4000-000000000001), [HT{ physical: 6000 }]) -> \
    SubDocKey(DocKey([], ["row1"]), [ColumnId(11)])
SubDocKey(DocKey([], ["row1"]), [ColumnId(11); HT{ physical: 6000 }]) -> \
    VECTOR_DATA(561000000020003000400000000000000111)
  )#");

  put_batch.Clear();
  put_batch.dup_delete_vector_ids(vector_id.AsSlice());
  ASSERT_OK(SendWriteBatch(put_batch, 7000_usec_ht, 6500_usec_ht));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
MetaKey(VectorId(10000000-2000-3000-4000-000000000001), [HT{ physical: 7000 }]) -> DEL
MetaKey(VectorId(10000000-2000-3000-4000-000000000001), [HT{ physical: 6000 }]) -> \
    SubDocKey(DocKey([], ["row1"]), [ColumnId(11)])
SubDocKey(DocKey([], ["row1"]), [ColumnId(11); HT{ physical: 6000 }]) -> \
    VECTOR_DATA(561000000020003000400000000000000111)
  )#");
}

// GH#32694: whenever the writer fills the intents write batch, it must clear the tablet's
// can_advance_intents_flush_op_id_.
TEST_F(NonTransactionalBatchWriterTest, ClearsCanAdvanceIntentsFlushOpId) {
  const Uuid involved_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  const TransactionId txn = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));
  const DocKey doc_key(MakeKeyEntryValues("row1"));
  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;

  std::atomic<bool> can_advance{true};

  // A batch that writes nothing to the intents db leaves the flag alone.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
  auto* write_pair = put_batch.add_write_pairs();
  write_pair->dup_key(
      EncodeDocPathKey(
          DocPath(doc_key.Encode(), KeyEntryValue::MakeColumnId(ColumnId(11)))).AsSlice());
  write_pair->dup_value(EncodeValue(QLValue::Primitive("value1")));
  ASSERT_OK(SendWriteBatch(
      put_batch, kWriteHT, kBatchHT, /* vector_indexes= */ nullptr, StorageSet::All(),
      TableType::PGSQL_TABLE_TYPE, &can_advance));
  ASSERT_TRUE(can_advance.load());

  // External intents are Put into the intents write batch (AddEntryToWriteBatch).
  put_batch.Clear();
  std::vector<ExternalIntent> intents = {
      {DocPath(doc_key.Encode()), EncodeValue(QLValue::Primitive("value2"))}};
  AddExternalIntentsWritePair(&put_batch, txn, kMinSubTransactionId, intents, involved_tablet);
  ASSERT_OK(SendWriteBatch(
      put_batch, kWriteHT, kBatchHT, /* vector_indexes= */ nullptr, StorageSet::All(),
      TableType::PGSQL_TABLE_TYPE, &can_advance));
  ASSERT_FALSE(can_advance.load());

  // Applying them SingleDeletes those intents into the intents write batch
  // (PrepareApplyExternalIntents).
  can_advance.store(true);
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn, kWriteHT);
  ASSERT_OK(SendWriteBatch(
      put_batch, kWriteHT, kBatchHT, /* vector_indexes= */ nullptr, StorageSet::All(),
      TableType::PGSQL_TABLE_TYPE, &can_advance));
  ASSERT_FALSE(can_advance.load());
}

}  // namespace yb::docdb
