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
//

#pragma once

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.fwd.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/intent.h"

#include "yb/rocksdb/write_batch.h"

namespace yb {
namespace docdb {

class NonTransactionalWriter : public rocksdb::DirectWriter {
 public:
  NonTransactionalWriter(
    std::reference_wrapper<const LWKeyValueWriteBatchPB> put_batch, HybridTime hybrid_time);

  bool Empty() const;

  Status Apply(rocksdb::DirectWriteHandler* handler) override;

 private:
  const LWKeyValueWriteBatchPB& put_batch_;
  HybridTime hybrid_time_;
};

// Buffer for encoding DocHybridTime
class DocHybridTimeBuffer {
 public:
  DocHybridTimeBuffer();

  Slice EncodeWithValueType(const DocHybridTime& doc_ht) {
    auto end = doc_ht.EncodedInDocDbFormat(buffer_.data() + 1);
    return Slice(buffer_.data(), end);
  }

  Slice EncodeWithValueType(HybridTime ht, IntraTxnWriteId write_id) {
    return EncodeWithValueType(DocHybridTime(ht, write_id));
  }
 private:
  std::array<char, 1 + kMaxBytesPerEncodedHybridTime> buffer_;
};

class TransactionalWriter : public rocksdb::DirectWriter {
 public:
  TransactionalWriter(
      std::reference_wrapper<const LWKeyValueWriteBatchPB> put_batch,
      HybridTime hybrid_time,
      const TransactionId& transaction_id,
      IsolationLevel isolation_level,
      dockv::PartialRangeKeyIntents partial_range_key_intents,
      const Slice& replicated_batches_state,
      IntraTxnWriteId intra_txn_write_id);

  Status Apply(rocksdb::DirectWriteHandler* handler) override;

  IntraTxnWriteId intra_txn_write_id() const {
    return intra_txn_write_id_;
  }

  void SetMetadataToStore(const LWTransactionMetadataPB* value) {
    metadata_to_store_ = value;
  }

 private:
  Status operator()(
      dockv::IntentTypeSet intent_types, dockv::AncestorDocKey ancestor_doc_key,
      dockv::FullDocKey full_doc_key, Slice value_slice, dockv::KeyBytes* key,
      dockv::LastKey last_key);

  Status Finish();
  Status AddWeakIntent(
      const std::pair<KeyBuffer, dockv::IntentTypeSet>& intent_and_types,
      const std::array<Slice, 4>& value,
      DocHybridTimeBuffer* doc_ht_buffer);

  const LWKeyValueWriteBatchPB& put_batch_;
  HybridTime hybrid_time_;
  TransactionId transaction_id_;
  IsolationLevel isolation_level_;
  dockv::PartialRangeKeyIntents partial_range_key_intents_;
  Slice replicated_batches_state_;
  IntraTxnWriteId intra_txn_write_id_;
  IntraTxnWriteId write_id_ = 0;
  const LWTransactionMetadataPB* metadata_to_store_ = nullptr;

  // TODO(dtxn) weak & strong intent in one batch.
  // TODO(dtxn) extract part of code knowing about intents structure to lower level.
  // Handler is initialized in Apply method, and not used after apply returns.
  rocksdb::DirectWriteHandler* handler_;
  RowMarkType row_mark_;
  SubTransactionId subtransaction_id_;
  std::unordered_map<KeyBuffer, dockv::IntentTypeSet, ByteBufferHash> weak_intents_;
};

// Base class used by IntentsWriter to handle found intents.
class IntentsWriterContext {
 public:
  explicit IntentsWriterContext(const TransactionId& transaction_id);

  virtual ~IntentsWriterContext() = default;

  // Called at the start of iteration. Passed key of the first found entry, if present.
  virtual void Start(const boost::optional<Slice>& first_key) {}

  // Called on every reverse index entry.
  // key - entry key.
  // value - entry value.
  // metadata - whether entry is metadata entry or not.
  // Returns true if we should interrupt iteration, false otherwise.
  virtual Result<bool> Entry(
      const Slice& key, const Slice& value, bool metadata,
      rocksdb::DirectWriteHandler* handler) = 0;

  virtual void Complete(rocksdb::DirectWriteHandler* handler) = 0;

  const TransactionId& transaction_id() const {
    return transaction_id_;
  }

  ApplyTransactionState& apply_state() {
    return apply_state_;
  }

  bool reached_records_limit() const {
    return left_records_ <= 0;
  }

  void RegisterRecord() {
    --left_records_;
  }

 protected:
  void SetApplyState(
      const Slice& key, IntraTxnWriteId write_id, const SubtxnSet& aborted) {
    apply_state_.key = key.ToBuffer();
    apply_state_.write_id = write_id;
    apply_state_.aborted = aborted;
  }

 private:
  TransactionId transaction_id_;
  ApplyTransactionState apply_state_;
  int64_t left_records_;
};

class IntentsWriter : public rocksdb::DirectWriter {
 public:
  IntentsWriter(const Slice& start_key,
                rocksdb::DB* intents_db,
                IntentsWriterContext* context);

  Status Apply(rocksdb::DirectWriteHandler* handler) override;

 private:
  Slice start_key_;
  rocksdb::DB* intents_db_;
  IntentsWriterContext& context_;
  dockv::KeyBytes txn_reverse_index_prefix_;
  Slice reverse_index_upperbound_;
  BoundedRocksDbIterator reverse_index_iter_;
};

class FrontierSchemaVersionUpdater {
 public:
  explicit FrontierSchemaVersionUpdater(SchemaPackingProvider* schema_packing_provider)
      : schema_packing_provider_(schema_packing_provider) {}

  void SetFrontiers(ConsensusFrontiers* frontiers) { frontiers_ = frontiers; }

 protected:
  Status UpdateSchemaVersion(Slice key, Slice value);
  void FlushSchemaVersion();

 private:
  SchemaPackingProvider* schema_packing_provider_;
  Uuid schema_version_table_ = Uuid::Nil();
  ColocationId schema_version_colocation_id_ = 0;
  SchemaVersion min_schema_version_ = std::numeric_limits<SchemaVersion>::max();
  SchemaVersion max_schema_version_ = std::numeric_limits<SchemaVersion>::min();
  ConsensusFrontiers* frontiers_ = nullptr;
};

class ApplyIntentsContext : public IntentsWriterContext, public FrontierSchemaVersionUpdater {
 public:
  ApplyIntentsContext(
      const TransactionId& transaction_id,
      const ApplyTransactionState* apply_state,
      const SubtxnSet& aborted,
      HybridTime commit_ht,
      HybridTime log_ht,
      const KeyBounds* key_bounds,
      SchemaPackingProvider* schema_packing_provider,
      rocksdb::DB* intents_db);

  void Start(const boost::optional<Slice>& first_key) override;

  Result<bool> Entry(
      const Slice& key, const Slice& value, bool metadata,
      rocksdb::DirectWriteHandler* handler) override;

  void Complete(rocksdb::DirectWriteHandler* handler) override;

 private:
  Result<bool> StoreApplyState(const Slice& key, rocksdb::DirectWriteHandler* handler);

  const ApplyTransactionState* apply_state_;
  const SubtxnSet& aborted_;
  HybridTime commit_ht_;
  HybridTime log_ht_;
  IntraTxnWriteId write_id_;
  const KeyBounds* key_bounds_;
  BoundedRocksDbIterator intent_iter_;
};

class RemoveIntentsContext : public IntentsWriterContext {
 public:
  explicit RemoveIntentsContext(const TransactionId& transaction_id, uint8_t reason);

  Result<bool> Entry(
      const Slice& key, const Slice& value, bool metadata,
      rocksdb::DirectWriteHandler* handler) override;

  void Complete(rocksdb::DirectWriteHandler* handler) override;
 private:
  uint8_t reason_;
};

// Usually put_batch contains only records that should be applied to regular DB.
// So apply_external_transactions will be empty and regular_entry will be true.
//
// But in general case on consumer side of CDC put_batch could contain various kinds of records,
// that should be applied into regular and intents db.
// They are:
// apply_external_transactions
//   The list of external transactions that should be applied.
//   For each such transaction we should lookup for existing external intents (stored in intents DB)
//   and convert them to Put command in regular_write_batch plus SingleDelete command in
//   intents_write_batch.
// write_pairs
//   Could contain regular entries, that should be stored into regular DB as is.
//   Also pair could contain external intents, that should be stored into intents DB.
//   But if apply_external_transactions contains transaction for those external intents, then
//   those intents will be applied directly to regular DB, avoiding unnecessary write to intents DB.
//   This case is very common for short running transactions.
class ExternalIntentsBatchWriter : public rocksdb::DirectWriter,
                                   public FrontierSchemaVersionUpdater {
 public:
  ExternalIntentsBatchWriter(
      std::reference_wrapper<const LWKeyValueWriteBatchPB> put_batch, HybridTime write_hybrid_time,
      HybridTime batch_hybrid_time, rocksdb::DB* intents_db,
      rocksdb::WriteBatch* intents_write_batch, SchemaPackingProvider* schema_packing_provider);
  bool Empty() const;

  Status Apply(rocksdb::DirectWriteHandler* handler) override;

 private:
  // Reads all stored external intents for provided transactions and prepares batches that will
  // apply them into regular db and remove from intents db.
  Status PrepareApplyExternalIntents(
      ExternalTxnApplyState* apply_external_transactions, rocksdb::DirectWriteHandler* handler);

  // Adds external pair to write batch.
  // Returns true if add was skipped because pair is a regular (non external) record.
  Result<bool> AddExternalPairToWriteBatch(
      const yb::docdb::LWKeyValuePairPB& kv_pair,
      ExternalTxnApplyState* apply_external_transactions,
      rocksdb::DirectWriteHandler* regular_write_handler, IntraTxnWriteId* write_id);

  // Parse the merged external intent value, and write them to regular writer handler. Also updates
  // min/max schema version.
  Status PrepareApplyExternalIntentsBatch(
      const Slice& original_input_value, ExternalTxnApplyStateData* apply_data,
      rocksdb::DirectWriteHandler* regular_write_handler);

 private:
  const LWKeyValueWriteBatchPB& put_batch_;
  HybridTime write_hybrid_time_;
  HybridTime batch_hybrid_time_;
  BoundedRocksDbIterator intents_db_iter_;
  Slice intents_db_iter_upperbound_;
  rocksdb::WriteBatch* intents_write_batch_;
};

} // namespace docdb
} // namespace yb
