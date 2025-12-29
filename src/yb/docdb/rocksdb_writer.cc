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

#include "yb/docdb/rocksdb_writer.h"

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <boost/logic/tribool.hpp>

#include "yb/common/row_mark.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_format.h"
#include "yb/docdb/kv_debug.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/intent.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/walltime.h"

#include "yb/rocksdb/options.h"

#include "yb/tablet/transaction_intent_applier.h"

#include "yb/util/bitmap.h"
#include "yb/util/debug-util.h"
#include "yb/util/fast_varint.h"
#include "yb/util/flags.h"
#include "yb/util/pb_util.h"

DEFINE_UNKNOWN_bool(enable_transaction_sealing, false,
            "Whether transaction sealing is enabled.");
DEFINE_UNKNOWN_int32(txn_max_apply_batch_records, 100000,
             "Max number of apply records allowed in single RocksDB batch. "
             "When a transaction's data in one tablet does not fit into specified number of "
             "records, it will be applied using multiple RocksDB write batches.");

DEFINE_test_flag(bool, docdb_sort_weak_intents, false,
                "Sort weak intents to make their order deterministic.");
DEFINE_test_flag(bool, fail_on_replicated_batch_idx_set_in_txn_record, false,
                 "Fail when a set of replicated batch indexes is found in txn record.");


namespace yb {
namespace docdb {

using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryTypeAsChar;
using dockv::ValueEntryTypeAsChar;

namespace {

constexpr char kPostApplyMetadataMarker = 0;

void DumpIntentsRecordForTransaction(rocksdb::DB* intents_db, const TransactionId& transaction_id,
                                     SchemaPackingProvider* schema_packing_provider = nullptr) {
  DumpIntentsContext context(transaction_id, intents_db, schema_packing_provider);
  IntentsWriter intents_writer(
      /* start_key = */ Slice(),
      /* file_filter_ht = */ HybridTime::kInvalid,
      intents_db,
      &context,
      /* ignore_metadata = */ false,
      /* key_to_apply = */ Slice());

  // Create a dummy handler since we're only reading/logging, not writing
  class DummyHandler : public rocksdb::DirectWriteHandler {
   public:
    std::pair<Slice, Slice> Put(const SliceParts& key, const SliceParts& value) override {
      return std::make_pair(Slice(), Slice());
    }
    void SingleDelete(const Slice& key) override {
    }
  };

  DummyHandler dummy_handler;
  Status status = intents_writer.Apply(dummy_handler);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to dump intents for transaction: " << transaction_id
                 << ", error: " << status;
  }
}

// Slice parts with the number of slices fixed at compile time.
template <int N>
struct FixedSliceParts {
  FixedSliceParts(const std::array<Slice, N>& input) : parts(input.data()) { // NOLINT
  }

  operator SliceParts() const {
    return SliceParts(parts, N);
  }

  const Slice* parts;
};

// Main intent data::
// Prefix + DocPath + IntentType + DocHybridTime -> TxnId + value of the intent
// Reverse index by txn id:
// Prefix + TxnId + DocHybridTime -> Main intent data key
//
// Expects that last entry of key is DocHybridTime.
template <int N>
void AddIntent(
    const TransactionId& transaction_id,
    const FixedSliceParts<N>& key,
    const SliceParts& value,
    rocksdb::DirectWriteHandler& handler,
    Slice reverse_value_prefix = Slice()) {
  char reverse_key_prefix[1] = { KeyEntryTypeAsChar::kTransactionId };
  dockv::DocHybridTimeWordBuffer doc_ht_buffer;
  auto doc_ht_slice = dockv::InvertEncodedDocHT(key.parts[N - 1], &doc_ht_buffer);

  std::array<Slice, 3> reverse_key = {{
      Slice(reverse_key_prefix, sizeof(reverse_key_prefix)),
      transaction_id.AsSlice(),
      doc_ht_slice,
  }};
  handler.Put(key, value);
  if (reverse_value_prefix.empty()) {
    handler.Put(reverse_key, key);
  } else {
    std::array<Slice, N + 1> reverse_value;
    reverse_value[0] = reverse_value_prefix;
    memcpy(&reverse_value[1], key.parts, sizeof(*key.parts) * N);
    handler.Put(reverse_key, reverse_value);
  }
}

template <size_t N>
void PutApplyState(
    const Slice& transaction_id_slice, HybridTime commit_ht, IntraTxnWriteId write_id,
    const std::array<Slice, N>& value_parts, rocksdb::DirectWriteHandler& handler) {
  char transaction_apply_state_value_type = KeyEntryTypeAsChar::kTransactionApplyState;
  char group_end_value_type = KeyEntryTypeAsChar::kGroupEnd;
  char hybrid_time_value_type = KeyEntryTypeAsChar::kHybridTime;
  DocHybridTime doc_hybrid_time(commit_ht, write_id);
  char doc_hybrid_time_buffer[kMaxBytesPerEncodedHybridTime];
  char* doc_hybrid_time_buffer_end = doc_hybrid_time.EncodedInDocDbFormat(
      doc_hybrid_time_buffer);
  std::array<Slice, 5> key_parts = {{
      Slice(&transaction_apply_state_value_type, 1),
      transaction_id_slice,
      Slice(&group_end_value_type, 1),
      Slice(&hybrid_time_value_type, 1),
      Slice(doc_hybrid_time_buffer, doc_hybrid_time_buffer_end),
  }};
  handler.Put(key_parts, value_parts);
}

void HandleRegularRecord(
    const yb::docdb::LWKeyValuePairPB& kv_pair, HybridTime hybrid_time,
    DocHybridTimeBuffer* doc_ht_buffer, rocksdb::DirectWriteHandler& handler,
    IntraTxnWriteId* write_id) {
#ifndef NDEBUG
  // Debug-only: ensure all keys we get in Raft replication can be decoded.
  dockv::SubDocKey subdoc_key;
  Status s = subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(kv_pair.key());
  CHECK(s.ok()) << "Failed decoding key: " << s.ToString() << "; "
                << "Problematic key: " << dockv::BestEffortDocDBKeyToStr(kv_pair.key()) << "\n"
                << "value: " << kv_pair.value().ToDebugHexString();
#endif

  // We replicate encoded SubDocKeys without a HybridTime at the end, and only append it here.
  // The reason for this is that the HybridTime timestamp is only picked at the time of
  // appending  an entry to the tablet's Raft log. Also this is a good way to save network
  // bandwidth.
  //
  // "Write id" is the final component of our HybridTime encoding (or, to be more precise,
  // DocHybridTime encoding) that helps disambiguate between different updates to the
  // same key (row/column) within a transaction. We set it based on the position of the write
  // operation in its write batch.

  auto record_hybrid_time =
      kv_pair.has_external_hybrid_time() ? HybridTime(kv_pair.external_hybrid_time()) : hybrid_time;
  std::array<Slice, 2> key_parts = {{
      Slice(kv_pair.key()),
      doc_ht_buffer->EncodeWithValueType(record_hybrid_time, *write_id),
  }};
  Slice key_value = kv_pair.value();
  handler.Put(key_parts, SliceParts(&key_value, 1));

  ++(*write_id);
}

[[nodiscard]] dockv::ReadIntentTypeSets GetIntentTypesForRead(
    IsolationLevel level, RowMarkType row_mark, bool read_is_for_write_op) {
  auto result = dockv::GetIntentTypesForRead(level, row_mark);
  if (read_is_for_write_op) {
    result.read.Set(dockv::IntentType::kStrongRead);
  }
  return result;
}

}  // namespace

NonTransactionalWriter::NonTransactionalWriter(
    std::reference_wrapper<const docdb::LWKeyValueWriteBatchPB> put_batch, HybridTime hybrid_time)
    : put_batch_(put_batch), hybrid_time_(hybrid_time) {}

bool NonTransactionalWriter::Empty() const { return put_batch_.write_pairs().empty(); }

Status NonTransactionalWriter::Apply(rocksdb::DirectWriteHandler& handler) {
  DocHybridTimeBuffer doc_ht_buffer;

  IntraTxnWriteId write_id = 0;
  for (const auto& kv_pair : put_batch_.write_pairs()) {
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());

    if (kv_pair.key()[0] == KeyEntryTypeAsChar::kExternalTransactionId) {
      continue;
    }

    HandleRegularRecord(kv_pair, hybrid_time_, &doc_ht_buffer, handler, &write_id);
  }

  return Status::OK();
}

TransactionalWriter::TransactionalWriter(
    std::reference_wrapper<const LWKeyValueWriteBatchPB> put_batch,
    HybridTime hybrid_time,
    const TransactionId& transaction_id,
    IsolationLevel isolation_level,
    dockv::PartialRangeKeyIntents partial_range_key_intents,
    const Slice& replicated_batches_state,
    IntraTxnWriteId intra_txn_write_id,
    tablet::TransactionIntentApplier* applier,
    dockv::SkipPrefixLocks skip_prefix_locks)
    : put_batch_(put_batch),
      hybrid_time_(hybrid_time),
      transaction_id_(transaction_id),
      isolation_level_(isolation_level),
      partial_range_key_intents_(partial_range_key_intents),
      replicated_batches_state_(replicated_batches_state),
      intra_txn_write_id_(intra_txn_write_id),
      applier_(applier),
      skip_prefix_locks_(skip_prefix_locks) {
}

// We have the following distinct types of data in this "intent store":
// Main intent data:
//   Prefix + SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value of the intent
// Transaction metadata
//   TxnId -> status tablet id + isolation level
// Reverse index by txn id
//   TxnId + HybridTime -> Main intent data key
// Post-apply transaction metadata
//   TxnId + kPostApplyMetadataMarker -> apply op id
//
// Where prefix is just a single byte prefix. TxnId, IntentType, HybridTime all prefixed with
// appropriate value type.
Status TransactionalWriter::Apply(rocksdb::DirectWriteHandler& handler) {
  VLOG(4) << "PrepareTransactionWriteBatch(), write_id = " << write_id_;

  row_mark_ = GetRowMarkTypeFromPB(put_batch_);
  handler_ = &handler;

  if (metadata_to_store_) {
    auto txn_value_type = KeyEntryTypeAsChar::kTransactionId;
    std::array<Slice, 2> key = {
      Slice(&txn_value_type, 1),
      transaction_id_.AsSlice(),
    };
    yb::LWTransactionMetadataPB data_copy(&metadata_to_store_->arena(), *metadata_to_store_);
    // We use hybrid time only for backward compatibility, actually wall time is required.
    data_copy.set_metadata_write_time(GetCurrentTimeMicros());
    data_copy.set_first_write_ht(hybrid_time_.ToUint64());
    auto value = data_copy.SerializeAsString();
    Slice value_slice(value);
    handler.Put(key, SliceParts(&value_slice, 1));
  }

  subtransaction_id_ = put_batch_.has_subtransaction()
      ? put_batch_.subtransaction().subtransaction_id()
      : kMinSubTransactionId;

  if (!put_batch_.write_pairs().empty()) {
    if (IsValidRowMarkType(row_mark_)) {
      LOG(WARNING) << "Performing a write with row lock " << RowMarkType_Name(row_mark_)
                   << " when only reads are expected";
    }

    // We cannot recover from failures here, because it means that we cannot apply replicated
    // operation.
    RETURN_NOT_OK(EnumerateIntents(
        put_batch_.write_pairs(),
        [this, intent_types = dockv::GetIntentTypesForWrite(isolation_level_)](
            auto ancestor_doc_key, auto full_doc_key, auto value, auto* key, auto last_key,
            auto is_row_lock, auto is_top_level_key, auto pk_is_known) {
          return (*this)(intent_types, ancestor_doc_key, full_doc_key, value, key, last_key,
              is_row_lock, is_top_level_key, pk_is_known);
        },
        partial_range_key_intents_,
        skip_prefix_locks_));
  }

  if (put_batch_.has_delete_vector_ids()) {
    auto key_type = KeyEntryTypeAsChar::kTransactionId;
    DocHybridTimeBuffer doc_ht_buffer;

    std::array<Slice, 3> key = {{
      Slice(&key_type, sizeof(key_type)),
      transaction_id_.AsSlice(),
      doc_ht_buffer.EncodeWithValueType(hybrid_time_, write_id_++),
    }};
    auto value_type = ValueEntryTypeAsChar::kDeleteVectorIds;
    std::array<Slice, 2> value = {{
      Slice(&value_type, sizeof(value_type)),
      Slice(put_batch_.delete_vector_ids()),
    }};

    handler.Put(key, value);
  }

  // Apply advisory locks.
  for (auto& lock_pair : put_batch_.lock_pairs()) {
    if (lock_pair.is_lock()) {
      KeyBytes key(lock_pair.lock().key());
      RETURN_NOT_OK((*this)(dockv::GetIntentTypesForLock(lock_pair.mode()),
          dockv::AncestorDocKey::kFalse, dockv::FullDocKey::kFalse,
          lock_pair.lock().value(), &key, dockv::LastKey::kTrue,
          dockv::IsRowLock::kFalse, dockv::IsTopLevelKey::kFalse, boost::indeterminate));
    } else {
      RSTATUS_DCHECK(applier_, IllegalState, "Can't apply unlock operation without applier");
      // Unlock operation.
      if (lock_pair.lock().key().empty()) {
        // Unlock All.
        RETURN_NOT_OK(applier_->RemoveAdvisoryLocks(transaction_id_, *handler_));
      } else {
        // Unlock a specific key.
        RETURN_NOT_OK(applier_->RemoveAdvisoryLock(transaction_id_, lock_pair.lock().key(),
            dockv::GetIntentTypesForLock(lock_pair.mode()), *handler_));
      }
    }
  }

  if (!put_batch_.read_pairs().empty()) {
    RETURN_NOT_OK(EnumerateIntents(
        put_batch_.read_pairs(),
        [this, intent_types = GetIntentTypesForRead(
                   isolation_level_, row_mark_,
                   !put_batch_.write_pairs().empty() /* read_is_for_write_op */)](
            auto ancestor_doc_key, auto full_doc_key, const auto& value, auto* key, auto last_key,
            auto is_row_lock, auto is_top_level_key, auto pk_is_known) {
          return (*this)(
              dockv::GetIntentTypes(intent_types, is_row_lock), ancestor_doc_key, full_doc_key,
              value, key, last_key, is_row_lock, is_top_level_key, pk_is_known);
        },
        partial_range_key_intents_,
        skip_prefix_locks_));
  }

  return Finish();
}

// Using operator() to pass this object conveniently to EnumerateIntents.
Status TransactionalWriter::operator()(
    dockv::IntentTypeSet intent_types, dockv::AncestorDocKey ancestor_doc_key, dockv::FullDocKey,
    Slice intent_value, dockv::KeyBytes* key, dockv::LastKey last_key, dockv::IsRowLock is_row_lock,
    dockv::IsTopLevelKey is_top_level_key, boost::tribool pk_is_known) {
  RSTATUS_DCHECK(intent_types.Any(), IllegalState, "Intent type set should not be empty");
  if (ancestor_doc_key) {
    const bool take_weak_lock = VERIFY_RESULT(ShouldTakeWeakLockForPrefix(
        ancestor_doc_key, is_top_level_key, skip_prefix_locks_, isolation_level_,
        pk_is_known, key));
    if (take_weak_lock) {
      weak_intents_[key->data()] |= take_weak_lock ? MakeWeak(intent_types) : intent_types;
      return Status::OK();
    }

    if (!is_top_level_key) {
      auto msg = Format("It is ancestor and trying take strong lock, but not top level key. "
          "skip_prefix_locks: $0, isolation_level: $1, key: $2",
          skip_prefix_locks_, isolation_level_, key->ToString());
      RSTATUS_DCHECK(false, InvalidArgument, msg);;
    }

    const bool slow_mode_si = skip_prefix_locks_ &&
        isolation_level_ == IsolationLevel::SERIALIZABLE_ISOLATION;
    if (!slow_mode_si) {
      auto msg = Format("Trying to take strong lock on a top level key,"
          "but it is not in the slow mode of serializable level. skip_prefix_locks: $0, "
          "isolation_level: $1, is_top_level_key: $2, key: $3",
          skip_prefix_locks_, isolation_level_, is_top_level_key, key->ToString());
      RSTATUS_DCHECK(false, InvalidArgument, msg);
    }
    if (is_row_lock) {
      static const Slice kRowLockValue{&dockv::ValueEntryTypeAsChar::kRowLock, 1};
      intent_value = kRowLockValue;
    }

  }

  const auto transaction_value_type = ValueEntryTypeAsChar::kTransactionId;
  const auto write_id_value_type = ValueEntryTypeAsChar::kWriteId;
  IntraTxnWriteId big_endian_write_id = BigEndian::FromHost32(intra_txn_write_id_);

  const auto subtransaction_value_type = KeyEntryTypeAsChar::kSubTransactionId;
  SubTransactionId big_endian_subtxn_id;
  Slice subtransaction_marker;
  Slice subtransaction_id;
  if (subtransaction_id_ > kMinSubTransactionId) {
    subtransaction_marker = Slice(&subtransaction_value_type, 1);
    big_endian_subtxn_id = BigEndian::FromHost32(subtransaction_id_);
    subtransaction_id = Slice::FromPod(&big_endian_subtxn_id);
  } else {
    DCHECK_EQ(subtransaction_id_, kMinSubTransactionId);
  }

  std::array<Slice, 7> value = {{
      Slice(&transaction_value_type, 1),
      transaction_id_.AsSlice(),
      subtransaction_marker,
      subtransaction_id,
      Slice(&write_id_value_type, 1),
      Slice::FromPod(&big_endian_write_id),
      intent_value,
  }};

  ++intra_txn_write_id_;

  char intent_type[2] = { KeyEntryTypeAsChar::kIntentTypeSet,
                          static_cast<char>(intent_types.ToUIntPtr()) };

  DocHybridTimeBuffer doc_ht_buffer;

  constexpr size_t kNumKeyParts = 3;
  std::array<Slice, kNumKeyParts> key_parts = {{
      key->AsSlice(),
      Slice(intent_type, 2),
      doc_ht_buffer.EncodeWithValueType(hybrid_time_, write_id_++),
  }};

  Slice reverse_value_prefix;
  if (last_key && FLAGS_enable_transaction_sealing) {
    reverse_value_prefix = replicated_batches_state_;
  }
  AddIntent<kNumKeyParts>(transaction_id_, key_parts, value, *handler_, reverse_value_prefix);
  return Status::OK();
}

Status TransactionalWriter::Finish() {
  char transaction_id_value_type = ValueEntryTypeAsChar::kTransactionId;

  DocHybridTimeBuffer doc_ht_buffer;

  const auto subtransaction_value_type = KeyEntryTypeAsChar::kSubTransactionId;
  SubTransactionId big_endian_subtxn_id;
  Slice subtransaction_marker;
  Slice subtransaction_id;
  if (subtransaction_id_ > kMinSubTransactionId) {
    subtransaction_marker = Slice(&subtransaction_value_type, 1);
    big_endian_subtxn_id = BigEndian::FromHost32(subtransaction_id_);
    subtransaction_id = Slice::FromPod(&big_endian_subtxn_id);
  } else {
    DCHECK_EQ(subtransaction_id_, kMinSubTransactionId);
  }

  std::array<Slice, 4> value = {{
      Slice(&transaction_id_value_type, 1),
      transaction_id_.AsSlice(),
      subtransaction_marker,
      subtransaction_id,
  }};

  if (PREDICT_FALSE(FLAGS_TEST_docdb_sort_weak_intents)) {
    // This is done in tests when deterministic DocDB state is required.
    std::vector<std::pair<KeyBuffer, dockv::IntentTypeSet>> intents_and_types(
        weak_intents_.begin(), weak_intents_.end());
    sort(intents_and_types.begin(), intents_and_types.end());
    for (const auto& intent_and_types : intents_and_types) {
      RETURN_NOT_OK(AddWeakIntent(intent_and_types, value, &doc_ht_buffer));
    }
    return Status::OK();
  }

  for (const auto& intent_and_types : weak_intents_) {
    RETURN_NOT_OK(AddWeakIntent(intent_and_types, value, &doc_ht_buffer));
  }

  return Status::OK();
}

Status TransactionalWriter::AddWeakIntent(
    const std::pair<KeyBuffer, dockv::IntentTypeSet>& intent_and_types,
    const std::array<Slice, 4>& value,
    DocHybridTimeBuffer* doc_ht_buffer) {
  char intent_type[2] = { KeyEntryTypeAsChar::kIntentTypeSet,
                          static_cast<char>(intent_and_types.second.ToUIntPtr()) };
  constexpr size_t kNumKeyParts = 3;
  std::array<Slice, kNumKeyParts> key = {{
      intent_and_types.first.AsSlice(),
      Slice(intent_type, 2),
      doc_ht_buffer->EncodeWithValueType(hybrid_time_, write_id_++),
  }};

  AddIntent<kNumKeyParts>(transaction_id_, key, value, *handler_);

  return Status::OK();
}

PostApplyMetadataWriter::PostApplyMetadataWriter(
    std::span<const PostApplyTransactionMetadata> metadatas)
    : metadatas_{metadatas} {
}

Status PostApplyMetadataWriter::Apply(rocksdb::DirectWriteHandler& handler) {
  ThreadSafeArena arena;
  for (const auto& metadata : metadatas_) {
    std::array<Slice, 3> metadata_key = {{
        Slice(&KeyEntryTypeAsChar::kTransactionId, 1),
        metadata.transaction_id.AsSlice(),
        Slice(&kPostApplyMetadataMarker, 1),
    }};

    LWPostApplyTransactionMetadataPB data(&arena);
    metadata.ToPB(&data);

    auto value = data.SerializeAsString();
    Slice value_slice{value};
    handler.Put(metadata_key, SliceParts(&value_slice, 1));
  }

  return Status::OK();
}

DocHybridTimeBuffer::DocHybridTimeBuffer() {
  buffer_[0] = KeyEntryTypeAsChar::kHybridTime;
}

IntentsWriterContext::IntentsWriterContext(
    const TransactionId& transaction_id, IgnoreMaxApplyLimit ignore_max_apply_limit)
    : transaction_id_(transaction_id),
      left_records_(ignore_max_apply_limit
          ? std::numeric_limits<decltype(left_records_)>::max()
          : FLAGS_txn_max_apply_batch_records) {
}

IntentsWriterContextBase::IntentsWriterContextBase(
    const TransactionId& transaction_id,
    IgnoreMaxApplyLimit ignore_max_apply_limit,
    rocksdb::DB* intents_db,
    const KeyBounds* key_bounds,
    HybridTime file_filter_ht)
    : IntentsWriterContext(transaction_id, ignore_max_apply_limit),
      intent_iter_(CreateRocksDBIterator(
          intents_db, key_bounds, BloomFilterOptions::Inactive(), rocksdb::kDefaultQueryId,
          CreateIntentHybridTimeFileFilter(file_filter_ht), /* iterate_upper_bound = */ nullptr,
          rocksdb::CacheRestartBlockKeys::kFalse)) {
}

IntentsWriter::IntentsWriter(const Slice& start_key,
                             HybridTime file_filter_ht,
                             rocksdb::DB* intents_db,
                             IntentsWriterContext* context,
                             bool ignore_metadata,
                             const Slice& key_to_apply)
    : start_key_(start_key), intents_db_(intents_db), context_(*context),
      ignore_metadata_(ignore_metadata), key_to_apply_(key_to_apply) {
  AppendTransactionKeyPrefix(context_.transaction_id(), &txn_reverse_index_prefix_);
  txn_reverse_index_prefix_.AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
  reverse_index_upperbound_ = txn_reverse_index_prefix_.AsSlice();

  reverse_index_iter_ = CreateRocksDBIterator(
      intents_db_, &KeyBounds::kNoBounds, BloomFilterOptions::Inactive(), rocksdb::kDefaultQueryId,
      CreateIntentHybridTimeFileFilter(file_filter_ht), &reverse_index_upperbound_,
      rocksdb::CacheRestartBlockKeys::kFalse);
}

Status IntentsWriter::Apply(rocksdb::DirectWriteHandler& handler) {
  Slice key_prefix = txn_reverse_index_prefix_.AsSlice();
  key_prefix.remove_suffix(1);

  DocHybridTimeBuffer doc_ht_buffer;

  reverse_index_iter_.Seek(start_key_.empty() ? key_prefix : start_key_);

  context_.Start(
      reverse_index_iter_.Valid() ? std::make_optional(reverse_index_iter_.key()) : std::nullopt);

  for (; reverse_index_iter_.Valid(); reverse_index_iter_.Next()) {
    const Slice key_slice(reverse_index_iter_.key());

    if (!key_slice.starts_with(key_prefix)) {
      break;
    }

    auto reverse_index_value = reverse_index_iter_.value();
    // Ignore keys other than key_to_apply_ if it's specified.
    if (!key_to_apply_.empty() && !reverse_index_value.starts_with(key_to_apply_)) {
      continue;
    }

    // Check if they key is transaction metadata (1 byte prefix + transaction id) or
    // post-apply transaction metadata (1 byte prefix + transaction id + 1 byte suffix).
    bool metadata = key_slice.size() == 1 + TransactionId::StaticSize() ||
                    key_slice.size() == 2 + TransactionId::StaticSize();
    if (ignore_metadata_ && metadata) {
      // For advisory lock Unlock all operation, we don't want to remove txn metadata entry.
      continue;
    }
    // At this point, txn_reverse_index_prefix is a prefix of key_slice. If key_slice is equal to
    // txn_reverse_index_prefix in size, then they are identical, and we are seeked to transaction
    // metadata. Otherwise, we're seeked to an intent entry in the index which we may process.
    if (!metadata) {
      if (reverse_index_value.TryConsumeByte(KeyEntryTypeAsChar::kBitSet)) {
        CHECK(!FLAGS_TEST_fail_on_replicated_batch_idx_set_in_txn_record);
        RETURN_NOT_OK(OneWayBitmap::Skip(&reverse_index_value));
      } else if (reverse_index_value.TryConsumeByte(ValueEntryTypeAsChar::kDeleteVectorIds)) {
        RETURN_NOT_OK(context_.DeleteVectorIds(key_slice, reverse_index_value, handler));
        continue;
      }
    }

    if (VERIFY_RESULT(context_.Entry(key_slice, reverse_index_value, metadata, handler))) {
      return context_.Complete(handler, /* transaction_finished= */ false);
    }

    // The first lock specified by key_to_apply_ was found and processed.
    if (!key_to_apply_.empty()) {
      key_applied_ = true;
      break;
    }
  }
  RETURN_NOT_OK(reverse_index_iter_.status());

  return context_.Complete(handler, /* transaction_finished= */ true);
}

ApplyIntentsContext::ApplyIntentsContext(
    const TabletId& tablet_id,
    const TransactionId& transaction_id,
    const ApplyTransactionState* apply_state,
    const SubtxnSet& aborted,
    HybridTime commit_ht,
    HybridTime log_ht,
    HybridTime file_filter_ht,
    const OpId& apply_op_id,
    const KeyBounds* key_bounds,
    SchemaPackingProvider& schema_packing_provider,
    ConsensusFrontiers& frontiers,
    rocksdb::DB* intents_db,
    const DocVectorIndexesPtr& vector_indexes,
    const docdb::StorageSet& apply_to_storages,
    ApplyIntentsContextCompleteListener complete_listener)
      // TODO(vector_index) Add support for large transactions.
    : IntentsWriterContextBase(transaction_id, IgnoreMaxApplyLimit(vector_indexes != nullptr),
          intents_db, key_bounds, file_filter_ht),
      FrontierSchemaVersionUpdater(schema_packing_provider, frontiers),
      tablet_id_(tablet_id),
      apply_state_(apply_state),
      // In case we have passed in a non-null apply_state, its aborted set will have been loaded
      // from persisted apply state, and the passed in aborted set will correspond to the aborted
      // set at commit time. Rather then copy that set upstream so it is passed in as aborted, we
      // simply grab a reference to it here, if it is defined, to use in this method.
      aborted_(apply_state ? apply_state->aborted : aborted),
      write_id_(apply_state ? apply_state->write_id : 0),
      commit_ht_(commit_ht),
      log_ht_(log_ht),
      apply_op_id_(apply_op_id),
      key_bounds_(key_bounds),
      vector_indexes_(vector_indexes),
      apply_to_storages_(apply_to_storages),
      complete_listener_(std::move(complete_listener)),
      intents_db_(intents_db) {
  if (vector_indexes_) {
    vector_index_batches_.resize(vector_indexes_->size());
  }
}

Result<bool> ApplyIntentsContext::StoreApplyState(
    const Slice& key, rocksdb::DirectWriteHandler& handler) {
  SetApplyState(key, write_id_, aborted_);
  ApplyTransactionStatePB pb;
  apply_state().ToPB(&pb);
  pb.set_commit_ht(commit_ht_.ToUint64());
  apply_op_id_.ToPB(pb.mutable_apply_op_id());
  faststring encoded_pb;
  RETURN_NOT_OK(pb_util::SerializeToString(pb, &encoded_pb));
  char string_value_type = ValueEntryTypeAsChar::kString;
  std::array<Slice, 2> value_parts = {{
    Slice(&string_value_type, 1),
    Slice(encoded_pb.data(), encoded_pb.size())
  }};
  VLOG_WITH_FUNC(4)
      << "TXN: " << transaction_id() << ", commit_ht: " << commit_ht_ << ", pb: " << AsString(pb);
  PutApplyState(transaction_id().AsSlice(), commit_ht_, write_id_, value_parts, handler);
  return true;
}

void ApplyIntentsContext::Start(const std::optional<Slice>& first_key) {
  if (!apply_state_) {
    return;
  }
  // This sanity check is invalid for remove case, because .SST file could be deleted.
  LOG_IF(DFATAL, !first_key || *first_key != apply_state_->key)
      << "Continue from wrong key: " << Slice(apply_state_->key).ToDebugString() << ", txn: "
      << transaction_id() << ", position: "
      << (first_key ? first_key->ToDebugString() : "<INVALID>")
      << ", write id: " << apply_state_->write_id;
}

Result<bool> ApplyIntentsContext::Entry(
    const Slice& key, const Slice& value, bool metadata, rocksdb::DirectWriteHandler& handler) {
  // Value of reverse index is a key of original intent record, so seek it and check match.
  if (metadata || !IsWithinBounds(key_bounds_, value)) {
    return false;
  }

  // We store apply state only if there are some more intents left.
  // So doing this check here, instead of right after write_id was incremented.
  if (reached_records_limit()) {
    return StoreApplyState(key, handler);
  }

  DocHybridTimeBuffer doc_ht_buffer;
  intent_iter_.Seek(value);
  if (!intent_iter_.Valid() || intent_iter_.key() != value) {
    Slice temp_slice = value;
    auto value_doc_ht = DocHybridTime::DecodeFromEnd(&temp_slice);
    temp_slice = key;
    auto key_doc_ht = DocHybridTime::DecodeFromEnd(&temp_slice);
    LOG(DFATAL) << "Unable to find intent: " << value.ToDebugHexString() << " ("
                << value_doc_ht << ") for " << key.ToDebugHexString() << "(" << key_doc_ht << ")";
    return false;
  }

  auto intent = VERIFY_RESULT(dockv::ParseIntentKey(value, transaction_id().AsSlice()));

  if (intent.types.Test(dockv::IntentType::kStrongWrite)) {
    const Slice transaction_id_slice = transaction_id().AsSlice();
    auto decoded_value = VERIFY_RESULT(dockv::DecodeIntentValue(
        intent_iter_.value(), &transaction_id_slice));
    // Dump the intents to log before crashing
    if (decoded_value.write_id < write_id_) {
      DumpIntentsRecordForTransaction(intents_db_, transaction_id(), &schema_packing_provider());

      // Write id should match to one that were calculated during append of intents.
      // Doing it just for sanity check.
      RSTATUS_DCHECK(
          false,
          Corruption,
          Format("Unexpected write id. Expected: $0, found: $1, raw value: $2",
                 write_id_,
                 decoded_value.write_id,
                 intent_iter_.value().ToDebugHexString()));
    }

    write_id_ = decoded_value.write_id;

    // Intents for row locks should be ignored (i.e. should not be written as regular records).
    if (decoded_value.body.starts_with(ValueEntryTypeAsChar::kRowLock)) {
      return false;
    }

    // Intents from aborted subtransactions should not be written as regular records.
    if (aborted_.Test(decoded_value.subtransaction_id)) {
      return false;
    }

    if (ApplyToRegularDB()) {
      // After strip of prefix and suffix intent_key contains just SubDocKey w/o a hybrid time.
      // Time will be added when writing batch to RocksDB.
      std::array<Slice, 2> key_parts = {{
          intent.doc_path,
          doc_ht_buffer.EncodeWithValueType(commit_ht_, write_id_),
      }};
      std::array<Slice, 2> value_parts = {{
          intent.doc_ht,
          decoded_value.body,
      }};

    // Useful when debugging transaction failure.
#if defined(DUMP_APPLY)
    dockv::SubDocKey sub_doc_key;
    CHECK_OK(sub_doc_key.FullyDecodeFrom(intent.doc_path, dockv::HybridTimeRequired::kFalse));
    if (!sub_doc_key.subkeys().empty()) {
      auto txn_id = FullyDecodeTransactionId(transaction_id_slice);
      LOG(INFO) << "Apply: " << sub_doc_key.ToString()
                << ", time: " << commit_ht << ", write id: " << *write_id << ", txn: " << txn_id
                << ", value: " << intent_value.ToDebugString();
    }
#endif
      handler.Put(key_parts, value_parts);
    }

    if (vector_indexes_) {
      RETURN_NOT_OK(ProcessVectorIndexes(handler, intent.doc_path, decoded_value.body));
    }

    ++write_id_;
    RegisterRecord();

    YB_TRANSACTION_DUMP(
        ApplyIntent, tablet_id_, transaction_id(), intent.doc_path.size(), intent.doc_path,
        commit_ht_, write_id_, decoded_value.body);

    RETURN_NOT_OK(UpdateSchemaVersion(intent.doc_path, decoded_value.body));
  }

  return false;
}

Status ApplyIntentsContext::ProcessVectorIndexes(
    rocksdb::DirectWriteHandler& handler, Slice key, Slice value) {
  if (value.starts_with(ValueEntryTypeAsChar::kTombstone)) {
    return Status::OK();
  }

  auto sizes = VERIFY_RESULT(dockv::DocKey::EncodedPrefixAndDocKeySizes(key));
  if (sizes.doc_key_size < key.size()) {
    auto entry_type = static_cast<KeyEntryType>(key[sizes.doc_key_size]);
    if (entry_type == KeyEntryType::kColumnId) {
      auto column_id = VERIFY_RESULT(ColumnId::FullyDecode(
          key.WithoutPrefix(sizes.doc_key_size + 1)));
      // We expect small amount of vector indexes, usually 1. So it is faster to iterate over them.
      bool need_reverse_entry = apply_to_storages_.TestRegularDB();
      for (size_t i = 0; i != vector_indexes_->size(); ++i) {
        const auto& vector_index = *(*vector_indexes_)[i];
        auto table_key_prefix = vector_index.indexed_table_key_prefix();
        if (key.starts_with(table_key_prefix) && vector_index.column_id() == column_id &&
            commit_ht_ > vector_index.hybrid_time()) {
          if (ApplyToVectorIndex(i)) {
            vector_index_batches_[i].push_back(DocVectorIndexInsertEntry {
              .value = ValueBuffer(value.WithoutPrefix(1)),
            });
          }
          if (need_reverse_entry) {
            auto ybctid = key.Prefix(sizes.doc_key_size).WithoutPrefix(table_key_prefix.size());
            DocVectorIndex::ApplyReverseEntry(
                handler, ybctid, value, DocHybridTime(commit_ht_, write_id_));
            need_reverse_entry = false;
          }
        }
      }
    } else {
      LOG_IF(DFATAL, entry_type != KeyEntryType::kSystemColumnId)
          << "Unexpected entry type: " << entry_type << " in " << key.ToDebugHexString();
    }
  } else {
    auto packed_row_version = dockv::GetPackedRowVersion(value);
    RSTATUS_DCHECK(packed_row_version.has_value(), Corruption,
                   "Full row with non packed value: $0 -> $1",
                   key.ToDebugHexString(), value.ToDebugHexString());
    switch (*packed_row_version) {
      case dockv::PackedRowVersion::kV1:
        return ProcessVectorIndexesForPackedRow<dockv::PackedRowDecoderV1>(
            handler, sizes.prefix_size, key, value);
      case dockv::PackedRowVersion::kV2:
        return ProcessVectorIndexesForPackedRow<dockv::PackedRowDecoderV2>(
            handler, sizes.prefix_size, key, value);
    }
    FATAL_INVALID_ENUM_VALUE(dockv::PackedRowVersion, *packed_row_version);
  }
  return Status::OK();
}

template <class Decoder>
Status ApplyIntentsContext::ProcessVectorIndexesForPackedRow(
    rocksdb::DirectWriteHandler& handler, size_t prefix_size, Slice key, Slice value) {
  value.consume_byte();

  auto schema_version = narrow_cast<SchemaVersion>(VERIFY_RESULT(FastDecodeUnsignedVarInt(&value)));

  auto table_key_prefix = key.Prefix(prefix_size);
  if (schema_packing_version_ != schema_version ||
      schema_packing_table_prefix_.AsSlice() != table_key_prefix) {
    auto packing = VERIFY_RESULT(prefix_size
        ? schema_packing_provider_.ColocationPacking(
              BigEndian::Load32(key.data() + 1), schema_version, HybridTime::kMax)
        : schema_packing_provider_.CotablePacking(
              Uuid::Nil(), schema_version, HybridTime::kMax));
    schema_packing_ = packing.schema_packing;
    schema_packing_version_ = schema_version;
    schema_packing_table_prefix_.Assign(table_key_prefix);
  }
  Decoder decoder(*schema_packing_, value.data());

  boost::dynamic_bitset<> columns_added_to_vector_index;
  for (size_t i = 0; i != vector_indexes_->size(); ++i) {
    const auto& vector_index = *(*vector_indexes_)[i];
    auto vector_index_table_key_prefix = vector_index.indexed_table_key_prefix();
    if (table_key_prefix != vector_index_table_key_prefix ||
        commit_ht_ <= vector_index.hybrid_time()) {
      continue;
    }
    auto column_value = decoder.FetchValue(vector_index.column_id());
    if (column_value.IsNull()) {
      VLOG_WITH_FUNC(3) << "Ignoring null vector value for key '" << key.ToDebugHexString() << "'";
      continue;
    }

    auto ybctid = key.WithoutPrefix(table_key_prefix.size());
    if (ApplyToVectorIndex(i)) {
      vector_index_batches_[i].push_back(DocVectorIndexInsertEntry {
        .value = ValueBuffer(column_value->WithoutPrefix(
            std::is_same_v<Decoder, dockv::PackedRowDecoderV2> ? 0 : 1)),
      });
    }

    if (apply_to_storages_.TestRegularDB()) {
      size_t column_index = schema_packing_->GetIndex(vector_index.column_id());
      columns_added_to_vector_index.resize(
          std::max(columns_added_to_vector_index.size(), column_index + 1));
      if (!columns_added_to_vector_index.test_set(column_index)) {
        DocVectorIndex::ApplyReverseEntry(
            handler, ybctid, *column_value, DocHybridTime(commit_ht_, write_id_));
      }
    }
  }
  return Status::OK();
}

Status ApplyIntentsContext::Complete(
    rocksdb::DirectWriteHandler& handler, bool transaction_finished) {
  if (transaction_finished) {
    if (apply_state_) {
      char tombstone_value_type = ValueEntryTypeAsChar::kTombstone;
      std::array<Slice, 1> value_parts = {{Slice(&tombstone_value_type, 1)}};
      PutApplyState(transaction_id().AsSlice(), commit_ht_, write_id_, value_parts, handler);
    }
  }
  if (vector_indexes_) {
    DocHybridTime write_time { commit_ht_, write_id_ };
    for (size_t i = 0; i != vector_index_batches_.size(); ++i) {
      if (!vector_index_batches_[i].empty()) {
        RETURN_NOT_OK((*vector_indexes_)[i]->Insert(vector_index_batches_[i], frontiers_));
      }
    }
  }
  FlushSchemaVersion();
  if (complete_listener_) {
    complete_listener_(frontiers_);
  }
  return Status::OK();
}

Status ApplyIntentsContext::DeleteVectorIds(
    Slice key, Slice ids, rocksdb::DirectWriteHandler& handler) {
  RSTATUS_DCHECK_EQ(
      ids.size() % vector_index::VectorId::StaticSize(), 0, Corruption,
      Format("Wrong size of deleted vector ids: $0", ids.ToDebugHexString()));
  DocHybridTimeBuffer ht_buf;
  auto encoded_write_time = ht_buf.EncodeWithValueType(DocHybridTime(commit_ht_, write_id_));
  char tombstone = dockv::ValueEntryTypeAsChar::kTombstone;
  Slice value(&tombstone, 1);
  while (ids.size() != 0) {
    auto id = ids.Prefix(vector_index::VectorId::StaticSize());
    handler.Put(dockv::DocVectorKeyAsParts(id, encoded_write_time), {&value, 1});
    ids.RemovePrefix(vector_index::VectorId::StaticSize());
  }
  frontiers_.Largest().SetHasVectorDeletion();
  return Status::OK();
}

Status FrontierSchemaVersionUpdater::UpdateSchemaVersion(Slice key, Slice value) {
  RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value));
  if (!IsPackedRow(dockv::DecodeValueEntryType(value))) {
    return Status::OK();
  }
  value.consume_byte();
  auto schema_version =
      narrow_cast<SchemaVersion>(VERIFY_RESULT(FastDecodeUnsignedVarInt(&value)));
  dockv::DocKeyDecoder decoder(key);
  auto cotable_id = Uuid::Nil();
  if (VERIFY_RESULT(decoder.DecodeCotableId(&cotable_id))) {
    schema_version_colocation_id_ = 0;
    if (cotable_id != schema_version_table_) {
      Status s = schema_packing_provider_.CheckCotablePacking(
          cotable_id, schema_version, HybridTime::kMax);
      if (PREDICT_FALSE(!s.ok())) {
        LOG_WITH_FUNC(DFATAL)
            << Format("Check cotable packing for cotable $0 with schema version $1 failed: $2",
                      cotable_id, schema_version, s);
        return s;
      }
      FlushSchemaVersion();
      schema_version_table_ = cotable_id;
    }
  } else {
    ColocationId colocation_id = 0;
    if (VERIFY_RESULT(decoder.DecodeColocationId(&colocation_id))) {
      if (colocation_id != schema_version_colocation_id_) {
        Status s = schema_packing_provider_.CheckColocationPacking(
            colocation_id, schema_version, HybridTime::kMax);
        if (PREDICT_FALSE(!s.ok())) {
          LOG_WITH_FUNC(DFATAL)
            << Format("Check colocation packing for colocation $0 with schema version $1"
                      "failed: $2",
                      colocation_id, schema_version, s);
        }
        FlushSchemaVersion();
        auto packing = schema_packing_provider_.ColocationPacking(
            colocation_id, kLatestSchemaVersion, HybridTime::kMax);
        if (packing.ok()) {
          cotable_id = packing->cotable_id;
          DCHECK(!cotable_id.IsNil()) << cotable_id.ToString();
          schema_version_table_ = cotable_id;
        } else if (packing.status().IsNotFound()) { // Table was deleted.
          schema_version_table_ = Uuid::Nil();
          schema_version_colocation_id_ = 0;
          return Status::OK();
        } else {
          return packing.status();
        }
        schema_version_colocation_id_ = colocation_id;
      }
    }
  }

  min_schema_version_ = std::min(min_schema_version_, schema_version);
  max_schema_version_ = std::max(max_schema_version_, schema_version);
  return Status::OK();
}

void FrontierSchemaVersionUpdater::FlushSchemaVersion() {
  if (min_schema_version_ > max_schema_version_) {
    return;
  }
  frontiers_.Smallest().UpdateSchemaVersion(
      schema_version_table_, min_schema_version_, rocksdb::UpdateUserValueType::kSmallest);
  frontiers_.Largest().UpdateSchemaVersion(
      schema_version_table_, max_schema_version_, rocksdb::UpdateUserValueType::kLargest);
  min_schema_version_ = std::numeric_limits<SchemaVersion>::max();
  max_schema_version_ = std::numeric_limits<SchemaVersion>::min();
}

RemoveIntentsContext::RemoveIntentsContext(const TransactionId& transaction_id, uint8_t reason)
    : IntentsWriterContext(transaction_id, IgnoreMaxApplyLimit::kFalse), reason_(reason) {
}

Result<bool> RemoveIntentsContext::Entry(
    const Slice& key, const Slice& value, bool metadata, rocksdb::DirectWriteHandler& handler) {
  if (reached_records_limit()) {
    SetApplyState(key, 0, SubtxnSet());
    return true;
  }

  handler.SingleDelete(key);
  YB_TRANSACTION_DUMP(RemoveIntent, transaction_id(), reason_, key);
  RegisterRecord();

  if (!metadata) {
    handler.SingleDelete(value);
    YB_TRANSACTION_DUMP(RemoveIntent, transaction_id(), reason_, value);
    RegisterRecord();
  }
  return false;
}

Status RemoveIntentsContext::Complete(
    rocksdb::DirectWriteHandler& handler, bool transaction_finished) {
  return Status::OK();
}

Status RemoveIntentsContext::DeleteVectorIds(
    Slice key, Slice ids, rocksdb::DirectWriteHandler& handler) {
  handler.SingleDelete(key);
  YB_TRANSACTION_DUMP(RemoveIntent, transaction_id(), reason_, key);
  RegisterRecord();
  return Status::OK();
}

struct ExternalTxnApplyStateData {
  HybridTime commit_ht;
  SubtxnSet aborted_subtransactions;
  IntraTxnWriteId write_id = 0;

  // Used by xCluster to only apply intents that match the key range of the matching producer
  // tablet.
  KeyBounds filter_range;
  bool filter_range_encoded;

  static Result<ExternalTxnApplyState> FromPB(const LWKeyValueWriteBatchPB& put_batch) {
    ExternalTxnApplyState result;
    for (const auto& apply : put_batch.apply_external_transactions()) {
      auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(apply.transaction_id()));
      ExternalTxnApplyStateData apply_data;
      apply_data.commit_ht = HybridTime(apply.commit_hybrid_time());
      apply_data.aborted_subtransactions =
          VERIFY_RESULT(SubtxnSet::FromPB(apply.aborted_subtransactions().set()));
      apply_data.filter_range = KeyBounds(apply.filter_start_key(), apply.filter_end_key());
      apply_data.filter_range_encoded = apply.filter_range_encoded();

      result.emplace(std::move(txn_id), std::move(apply_data));
    }

    return result;
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(commit_ht, aborted_subtransactions, write_id);
  }

  // Check the the encoded key is within the bounds of the xCluster producer tablet key range that
  // replicated the APPLY.
  Result<bool> IsWithinBounds(Slice encoded_key) const {
    if (filter_range_encoded) {
      return filter_range.IsWithinBounds(encoded_key);
    }

    // (DEPRECATE_EOL 2.27) Handle the case when producer is still on an older version.
    // Remove the key entry prefix byte(s) and verify the key is valid.
    auto output_key_value = encoded_key;
    auto output_key_value_byte = dockv::ConsumeKeyEntryType(&output_key_value);
    SCHECK_NE(output_key_value_byte, KeyEntryType::kInvalid, Corruption, "Wrong first byte");
    if (output_key_value_byte != KeyEntryType::kUInt16Hash) {
      DCHECK(false) << "Expected hash partitioned key, but got: " << output_key_value_byte;
      YB_LOG_EVERY_N_SECS(WARNING, 600)
          << "Non Hash partitioned record of key type '" << output_key_value_byte
          << "' detected on xCluster target. Report to the YugabyteDB support team if this error "
             "persists even after both xCluster Universes have been upgraded to the latest version";
    }
    return filter_range.IsWithinBounds(output_key_value);
  }
};

NonTransactionalBatchWriter::NonTransactionalBatchWriter(
    std::reference_wrapper<const LWKeyValueWriteBatchPB> put_batch, HybridTime write_hybrid_time,
    HybridTime batch_hybrid_time, rocksdb::DB* intents_db, rocksdb::WriteBatch* intents_write_batch,
    SchemaPackingProvider& schema_packing_provider, ConsensusFrontiers& frontiers)
    : FrontierSchemaVersionUpdater(schema_packing_provider, frontiers),
      put_batch_(put_batch),
      write_hybrid_time_(write_hybrid_time),
      batch_hybrid_time_(batch_hybrid_time),
      intents_write_batch_(intents_write_batch) {
  if (put_batch_.apply_external_transactions().size() > 0) {
    intents_db_iter_ = CreateRocksDBIterator(
        intents_db, &docdb::KeyBounds::kNoBounds, BloomFilterOptions::Inactive(),
        rocksdb::kDefaultQueryId, /* read_filter= */ nullptr, &intents_db_iter_upperbound_,
        rocksdb::CacheRestartBlockKeys::kFalse);
  }
}

bool NonTransactionalBatchWriter::Empty() const {
  return !put_batch_.write_pairs_size() && !put_batch_.apply_external_transactions_size();
}

namespace {

Status NotEnoughBytes(size_t present, size_t required, const Slice& full) {
  return STATUS_FORMAT(
      Corruption, "Not enough bytes in external intents $0 while $1 expected, full: $2", present,
      required, full.ToDebugHexString());
}

}  // namespace

Result<bool> NonTransactionalBatchWriter::PrepareApplyExternalIntentsBatch(
    const Slice& original_input_value, ExternalTxnApplyStateData* apply_data,
    rocksdb::DirectWriteHandler& regular_write_handler) {
  bool can_delete_entire_batch = true;
  auto input_value = original_input_value;
  DocHybridTimeBuffer doc_ht_buffer;
  RETURN_NOT_OK(input_value.consume_byte(KeyEntryTypeAsChar::kUuid));
  RETURN_NOT_OK(Uuid::FromSlice(input_value.Prefix(kUuidSize)));
  input_value.remove_prefix(kUuidSize);
  char header_byte = input_value.consume_byte();
  if (header_byte != KeyEntryTypeAsChar::kExternalIntents &&
      header_byte != KeyEntryTypeAsChar::kSubTransactionId) {
    return STATUS_FORMAT(
        Corruption, "Wrong first byte, expected $0 or $1 but found $2",
        static_cast<int>(KeyEntryTypeAsChar::kExternalIntents),
        static_cast<int>(KeyEntryTypeAsChar::kSubTransactionId), header_byte);
  }
  SubTransactionId subtransaction_id = kMinSubTransactionId;
  if (header_byte == KeyEntryTypeAsChar::kSubTransactionId) {
    subtransaction_id = Load<SubTransactionId, BigEndian>(input_value.data());
    input_value.remove_prefix(sizeof(SubTransactionId));
    RETURN_NOT_OK(input_value.consume_byte(KeyEntryTypeAsChar::kExternalIntents));
  }
  if (apply_data->aborted_subtransactions.Test(subtransaction_id)) {
    // Skip applying provisional writes that belong to subtransactions that got aborted.
    return can_delete_entire_batch;
  }
  for (;;) {
    auto key_size = VERIFY_RESULT(FastDecodeUnsignedVarInt(&input_value));
    if (key_size == 0) {
      break;
    }
    if (input_value.size() < key_size) {
      return NotEnoughBytes(input_value.size(), key_size, original_input_value);
    }
    auto output_key = input_value.Prefix(key_size);
    input_value.remove_prefix(key_size);
    auto value_size = VERIFY_RESULT(FastDecodeUnsignedVarInt(&input_value));
    if (input_value.size() < value_size) {
      return NotEnoughBytes(input_value.size(), value_size, original_input_value);
    }
    auto output_value = input_value.Prefix(value_size);
    input_value.remove_prefix(value_size);

    if (!VERIFY_RESULT(apply_data->IsWithinBounds(output_key))) {
      VLOG(1) << "Skipping APPLY of external intent with key: " << output_key.ToDebugHexString()
              << " since it is outside of filter range: " << apply_data->filter_range.ToString();
      // Skip this entry. Ensure that we don't delete this batch, as another apply will need this
      // skipped intent.
      can_delete_entire_batch = false;
      continue;
    }
    // Since external intents only contain one key since D24185, this should be all or nothing.
    DCHECK(can_delete_entire_batch);

    std::array<Slice, 2> key_parts = {{
        output_key,
        doc_ht_buffer.EncodeWithValueType(apply_data->commit_ht, apply_data->write_id),
    }};
    std::array<Slice, 1> value_parts = {{
        output_value,
    }};
    regular_write_handler.Put(key_parts, value_parts);
    ++apply_data->write_id;

    // Update min/max schema version.
    RETURN_NOT_OK(UpdateSchemaVersion(output_key, output_value));
  }

  return can_delete_entire_batch;
}

// Reads all stored external intents for provided transactions and prepares batches that will apply
// them into regular db and remove from intents db.
Status NonTransactionalBatchWriter::PrepareApplyExternalIntents(
    ExternalTxnApplyState* apply_external_transactions, rocksdb::DirectWriteHandler& handler) {
  KeyBytes key_prefix;
  KeyBytes key_upperbound;

  for (auto& [transaction_id, apply_data] : *apply_external_transactions) {
    key_prefix.Clear();
    key_prefix.AppendKeyEntryType(KeyEntryType::kExternalTransactionId);
    key_prefix.AppendRawBytes(transaction_id.AsSlice());

    key_upperbound = key_prefix;
    key_upperbound.AppendKeyEntryType(KeyEntryType::kMaxByte);
    intents_db_iter_upperbound_ = key_upperbound.AsSlice();

    intents_db_iter_.Seek(key_prefix);
    while (intents_db_iter_.Valid()) {
      const Slice input_key(intents_db_iter_.key());

      if (!input_key.starts_with(key_prefix.AsSlice())) {
        break;
      }

      // Returns whether or not we filtered out any intents, if we did then do not delete as a later
      // apply (with a different filter) might still need it.
      bool can_delete_entire_batch = VERIFY_RESULT(
          PrepareApplyExternalIntentsBatch(intents_db_iter_.value(), &apply_data, handler));

      if (can_delete_entire_batch) {
        intents_write_batch_->SingleDelete(input_key);
      }

      intents_db_iter_.Next();
    }
    RETURN_NOT_OK(intents_db_iter_.status());
  }

  return Status::OK();
}

Result<bool> NonTransactionalBatchWriter::AddEntryToWriteBatch(
    const yb::docdb::LWKeyValuePairPB& kv_pair, ExternalTxnApplyState* apply_external_transactions,
    rocksdb::DirectWriteHandler& regular_write_handler, IntraTxnWriteId* write_id) {
  SCHECK(!kv_pair.key().empty(), InvalidArgument, "Write pair key cannot be empty.");
  SCHECK(!kv_pair.value().empty(), InvalidArgument, "Write pair value cannot be empty.");

  if (kv_pair.key()[0] != KeyEntryTypeAsChar::kExternalTransactionId) {
    return true;
  }

  // We replicate encoded SubDocKeys without a HybridTime at the end, and only append it here.
  // The reason for this is that the HybridTime timestamp is only picked at the time of
  // appending  an entry to the tablet's Raft log. Also this is a good way to save network
  // bandwidth.
  //
  // "Write id" is the final component of our HybridTime encoding (or, to be more precise,
  // DocHybridTime encoding) that helps disambiguate between different updates to the
  // same key (row/column) within a transaction. We set it based on the position of the write
  // operation in its write batch.
  Slice key_value = kv_pair.value();
  // This entry contains external intents.
  Slice key = kv_pair.key();
  key.consume_byte();
  auto txn_id = VERIFY_RESULT(DecodeTransactionId(&key));
  auto it = apply_external_transactions->find(txn_id);
  if (it != apply_external_transactions->end()) {
    // The same write operation could contain external intents and instruct us to apply them.
    RETURN_NOT_OK(PrepareApplyExternalIntentsBatch(key_value, &it->second, regular_write_handler));
    return false;
  }

  // Use our local batch hybrid time and WriteId from this batch. Each batch is guaranteed to have a
  // unique hybrid time, so this is guaranteed to be unique for this tablet.
  // We do not use external hybrid time as we can have two producer tablets replicating to the same
  // consumer tablet, in which case both can have the same transaction id and external hybrid time
  // values.
  // Note: This is not idempotent. If the same external intent is written multiple times, it
  // will have multiple entries in intents db each with a different local hybrid time. The
  // duplicates will get removed when we APPLY the transaction and move the records to regular db.
  // Since all the records will have the same commit hybrid time, they will be deduped.
  DocHybridTimeBuffer doc_ht_buffer;
  dockv::DocHybridTimeWordBuffer inverted_doc_ht_buffer;
  std::array<Slice, 2> key_parts = {{
      Slice(kv_pair.key()),
      doc_ht_buffer.EncodeWithValueType(batch_hybrid_time_, *write_id),
  }};
  key_parts[1] = dockv::InvertEncodedDocHT(key_parts[1], &inverted_doc_ht_buffer);
  constexpr size_t kNumValueParts = 1;
  intents_write_batch_->Put(key_parts, {&key_value, kNumValueParts});
  ++(*write_id);

  return false;
}

Status NonTransactionalBatchWriter::Apply(rocksdb::DirectWriteHandler& handler) {
  auto apply_external_transactions = VERIFY_RESULT(ExternalTxnApplyStateData::FromPB(put_batch_));
  if (!apply_external_transactions.empty()) {
    DCHECK(intents_db_iter_.Initialized());
    RETURN_NOT_OK(PrepareApplyExternalIntents(&apply_external_transactions, handler));
  }

  DocHybridTimeBuffer doc_ht_buffer;
  IntraTxnWriteId write_id = 0;
  for (const auto& write_pair : put_batch_.write_pairs()) {
    if (VERIFY_RESULT(AddEntryToWriteBatch(
            write_pair, &apply_external_transactions, handler, &write_id))) {
      HandleRegularRecord(write_pair, write_hybrid_time_, &doc_ht_buffer, handler, &write_id);

      RETURN_NOT_OK(UpdateSchemaVersion(write_pair.key(), write_pair.value()));
    }
  }

  return Status::OK();
}

DumpIntentsContext::DumpIntentsContext(
    const TransactionId& transaction_id,
    rocksdb::DB* intents_db,
    SchemaPackingProvider* schema_packing_provider)
    : IntentsWriterContextBase(transaction_id, IgnoreMaxApplyLimit::kTrue,
                                   intents_db, &KeyBounds::kNoBounds, HybridTime::kInvalid),
      schema_packing_provider_(schema_packing_provider) {
}

void DumpIntentsContext::Start(const std::optional<Slice>& first_key) {
  LOG(INFO) << "Dumping intents for transaction: " << transaction_id();
}

Result<bool> DumpIntentsContext::Entry(const Slice& reverse_index_key,
                                       const Slice& reverse_index_value, bool metadata,
                                       rocksdb::DirectWriteHandler& handler) {
  auto dump_record = [this](const Slice& key, const Slice& val) {
    LOG(INFO) << "Intent record " << ++intent_count_ << ". "
              << docdb::EntryToString(key, val, schema_packing_provider_, StorageDbType::kIntents);
  };
  dump_record(reverse_index_key, reverse_index_value);

  if (metadata) {
    return false;
  }

  intent_iter_.Seek(reverse_index_value);
  if (!intent_iter_.Valid() || intent_iter_.key() != reverse_index_value) {
    LOG(WARNING) << "Missing intent: " << reverse_index_value.ToDebugHexString();
    return false;
  }

  dump_record(intent_iter_.key(), intent_iter_.value());
  return false;
}

Status DumpIntentsContext::DeleteVectorIds(
    Slice key, Slice ids, rocksdb::DirectWriteHandler& handler) {
  return Status::OK();
}

Status DumpIntentsContext::Complete(rocksdb::DirectWriteHandler& handler,
                                    bool transaction_finished) {
  LOG(INFO) << "Finished dumping intents for transaction: " << transaction_id()
            << ", total records: " << intent_count_
            << ", transaction finished: " << transaction_finished;
  return Status::OK();
}

}  // namespace docdb
} // namespace yb
