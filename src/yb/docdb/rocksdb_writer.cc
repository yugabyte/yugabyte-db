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

#include "yb/docdb/rocksdb_writer.h"

#include "yb/common/row_mark.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/kv_debug.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/intent.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/walltime.h"

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
    rocksdb::DirectWriteHandler* handler,
    Slice reverse_value_prefix = Slice()) {
  char reverse_key_prefix[1] = { KeyEntryTypeAsChar::kTransactionId };
  dockv::DocHybridTimeWordBuffer doc_ht_buffer;
  auto doc_ht_slice = dockv::InvertEncodedDocHT(key.parts[N - 1], &doc_ht_buffer);

  std::array<Slice, 3> reverse_key = {{
      Slice(reverse_key_prefix, sizeof(reverse_key_prefix)),
      transaction_id.AsSlice(),
      doc_ht_slice,
  }};
  handler->Put(key, value);
  if (reverse_value_prefix.empty()) {
    handler->Put(reverse_key, key);
  } else {
    std::array<Slice, N + 1> reverse_value;
    reverse_value[0] = reverse_value_prefix;
    memcpy(&reverse_value[1], key.parts, sizeof(*key.parts) * N);
    handler->Put(reverse_key, reverse_value);
  }
}

template <size_t N>
void PutApplyState(
    const Slice& transaction_id_slice, HybridTime commit_ht, IntraTxnWriteId write_id,
    const std::array<Slice, N>& value_parts, rocksdb::DirectWriteHandler* handler) {
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
  handler->Put(key_parts, value_parts);
}

void HandleExternalRecord(
    const yb::docdb::LWKeyValuePairPB& kv_pair, HybridTime hybrid_time,
    DocHybridTimeBuffer* doc_ht_buffer, rocksdb::DirectWriteHandler* handler,
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
  handler->Put(key_parts, SliceParts(&key_value, 1));

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

Status NonTransactionalWriter::Apply(rocksdb::DirectWriteHandler* handler) {
  DocHybridTimeBuffer doc_ht_buffer;

  IntraTxnWriteId write_id = 0;
  for (const auto& kv_pair : put_batch_.write_pairs()) {
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());

    if (kv_pair.key()[0] == KeyEntryTypeAsChar::kExternalTransactionId) {
      continue;
    }

    HandleExternalRecord(kv_pair, hybrid_time_, &doc_ht_buffer, handler, &write_id);
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
    IntraTxnWriteId intra_txn_write_id)
    : put_batch_(put_batch),
      hybrid_time_(hybrid_time),
      transaction_id_(transaction_id),
      isolation_level_(isolation_level),
      partial_range_key_intents_(partial_range_key_intents),
      replicated_batches_state_(replicated_batches_state),
      intra_txn_write_id_(intra_txn_write_id) {
}

// We have the following distinct types of data in this "intent store":
// Main intent data:
//   Prefix + SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value of the intent
// Transaction metadata
//   TxnId -> status tablet id + isolation level
// Reverse index by txn id
//   TxnId + HybridTime -> Main intent data key
//
// Where prefix is just a single byte prefix. TxnId, IntentType, HybridTime all prefixed with
// appropriate value type.
Status TransactionalWriter::Apply(rocksdb::DirectWriteHandler* handler) {
  VLOG(4) << "PrepareTransactionWriteBatch(), write_id = " << write_id_;

  row_mark_ = GetRowMarkTypeFromPB(put_batch_);
  handler_ = handler;

  if (metadata_to_store_) {
    auto txn_value_type = KeyEntryTypeAsChar::kTransactionId;
    std::array<Slice, 2> key = {
      Slice(&txn_value_type, 1),
      transaction_id_.AsSlice(),
    };
    yb::LWTransactionMetadataPB data_copy(&metadata_to_store_->arena(), *metadata_to_store_);
    // We use hybrid time only for backward compatibility, actually wall time is required.
    data_copy.set_metadata_write_time(GetCurrentTimeMicros());
    auto value = data_copy.SerializeAsString();
    Slice value_slice(value);
    handler->Put(key, SliceParts(&value_slice, 1));
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
            auto ancestor_doc_key, auto full_doc_key, auto value, auto* key, auto last_key, auto) {
          return (*this)(intent_types, ancestor_doc_key, full_doc_key, value, key, last_key);
        },
        partial_range_key_intents_));
  }

  if (!put_batch_.read_pairs().empty()) {
    RETURN_NOT_OK(EnumerateIntents(
        put_batch_.read_pairs(),
        [this, intent_types = GetIntentTypesForRead(
                   isolation_level_, row_mark_,
                   !put_batch_.write_pairs().empty() /* read_is_for_write_op */)](
            auto ancestor_doc_key, auto full_doc_key, const auto& value, auto* key, auto last_key,
            auto is_row_lock) {
          return (*this)(
              dockv::GetIntentTypes(intent_types, is_row_lock), ancestor_doc_key, full_doc_key,
              value, key, last_key);
        },
        partial_range_key_intents_));
  }

  return Finish();
}

// Using operator() to pass this object conveniently to EnumerateIntents.
Status TransactionalWriter::operator()(
    dockv::IntentTypeSet intent_types, dockv::AncestorDocKey ancestor_doc_key, dockv::FullDocKey,
    Slice intent_value, dockv::KeyBytes* key, dockv::LastKey last_key) {
  RSTATUS_DCHECK(intent_types.Any(), IllegalState, "Intent type set should not be empty");
  if (ancestor_doc_key) {
    weak_intents_[key->data()] |= MakeWeak(intent_types);
    return Status::OK();
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
  AddIntent<kNumKeyParts>(transaction_id_, key_parts, value, handler_, reverse_value_prefix);

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

  AddIntent<kNumKeyParts>(transaction_id_, key, value, handler_);

  return Status::OK();
}

DocHybridTimeBuffer::DocHybridTimeBuffer() {
  buffer_[0] = KeyEntryTypeAsChar::kHybridTime;
}

IntentsWriterContext::IntentsWriterContext(const TransactionId& transaction_id)
    : transaction_id_(transaction_id),
      left_records_(FLAGS_txn_max_apply_batch_records) {
}

IntentsWriter::IntentsWriter(const Slice& start_key,
                             rocksdb::DB* intents_db,
                             IntentsWriterContext* context)
    : start_key_(start_key), intents_db_(intents_db), context_(*context) {
  AppendTransactionKeyPrefix(context_.transaction_id(), &txn_reverse_index_prefix_);
  txn_reverse_index_prefix_.AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
  reverse_index_upperbound_ = txn_reverse_index_prefix_.AsSlice();
  reverse_index_iter_ = CreateRocksDBIterator(
      intents_db_, &KeyBounds::kNoBounds, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none,
      rocksdb::kDefaultQueryId, nullptr /* read_filter */, &reverse_index_upperbound_);
}

Status IntentsWriter::Apply(rocksdb::DirectWriteHandler* handler) {
  Slice key_prefix = txn_reverse_index_prefix_.AsSlice();
  key_prefix.remove_suffix(1);

  DocHybridTimeBuffer doc_ht_buffer;

  reverse_index_iter_.Seek(start_key_.empty() ? key_prefix : start_key_);

  context_.Start(
      reverse_index_iter_.Valid() ? boost::make_optional(reverse_index_iter_.key()) : boost::none);

  while (reverse_index_iter_.Valid()) {
    const Slice key_slice(reverse_index_iter_.key());

    if (!key_slice.starts_with(key_prefix)) {
      break;
    }

    auto reverse_index_value = reverse_index_iter_.value();

    bool metadata = key_slice.size() == 1 + TransactionId::StaticSize();
    // At this point, txn_reverse_index_prefix is a prefix of key_slice. If key_slice is equal to
    // txn_reverse_index_prefix in size, then they are identical, and we are seeked to transaction
    // metadata. Otherwise, we're seeked to an intent entry in the index which we may process.
    if (!metadata) {
      if (!reverse_index_value.empty() && reverse_index_value[0] == KeyEntryTypeAsChar::kBitSet) {
        CHECK(!FLAGS_TEST_fail_on_replicated_batch_idx_set_in_txn_record);
        reverse_index_value.remove_prefix(1);
        RETURN_NOT_OK(OneWayBitmap::Skip(&reverse_index_value));
      }
    }

    if (VERIFY_RESULT(context_.Entry(key_slice, reverse_index_value, metadata, handler))) {
      return Status::OK();
    }

    reverse_index_iter_.Next();
  }
  RETURN_NOT_OK(reverse_index_iter_.status());

  context_.Complete(handler);

  return Status::OK();
}

ApplyIntentsContext::ApplyIntentsContext(
    const TransactionId& transaction_id,
    const ApplyTransactionState* apply_state,
    const SubtxnSet& aborted,
    HybridTime commit_ht,
    HybridTime log_ht,
    const KeyBounds* key_bounds,
    SchemaPackingProvider* schema_packing_provider,
    rocksdb::DB* intents_db)
    : IntentsWriterContext(transaction_id),
      FrontierSchemaVersionUpdater(schema_packing_provider),
      apply_state_(apply_state),
      // In case we have passed in a non-null apply_state, its aborted set will have been loaded
      // from persisted apply state, and the passed in aborted set will correspond to the aborted
      // set at commit time. Rather then copy that set upstream so it is passed in as aborted, we
      // simply grab a reference to it here, if it is defined, to use in this method.
      aborted_(apply_state ? apply_state->aborted : aborted),
      commit_ht_(commit_ht),
      log_ht_(log_ht),
      write_id_(apply_state ? apply_state->write_id : 0),
      key_bounds_(key_bounds),
      intent_iter_(CreateRocksDBIterator(
          intents_db, key_bounds, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none,
          rocksdb::kDefaultQueryId)) {
}

Result<bool> ApplyIntentsContext::StoreApplyState(
    const Slice& key, rocksdb::DirectWriteHandler* handler) {
  SetApplyState(key, write_id_, aborted_);
  ApplyTransactionStatePB pb;
  apply_state().ToPB(&pb);
  pb.set_commit_ht(commit_ht_.ToUint64());
  faststring encoded_pb;
  RETURN_NOT_OK(pb_util::SerializeToString(pb, &encoded_pb));
  char string_value_type = ValueEntryTypeAsChar::kString;
  std::array<Slice, 2> value_parts = {{
    Slice(&string_value_type, 1),
    Slice(encoded_pb.data(), encoded_pb.size())
  }};
  PutApplyState(transaction_id().AsSlice(), commit_ht_, write_id_, value_parts, handler);
  return true;
}

void ApplyIntentsContext::Start(const boost::optional<Slice>& first_key) {
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
    const Slice& key, const Slice& value, bool metadata, rocksdb::DirectWriteHandler* handler) {
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

  auto intent = VERIFY_RESULT(ParseIntentKey(value, transaction_id().AsSlice()));

  if (intent.types.Test(dockv::IntentType::kStrongWrite)) {
    const Slice transaction_id_slice = transaction_id().AsSlice();
    auto decoded_value = VERIFY_RESULT(dockv::DecodeIntentValue(
        intent_iter_.value(), &transaction_id_slice));

    // Write id should match to one that were calculated during append of intents.
    // Doing it just for sanity check.
    RSTATUS_DCHECK_GE(
        decoded_value.write_id, write_id_,
        Corruption,
        Format("Unexpected write id. Expected: $0, found: $1, raw value: $2",
               write_id_,
               decoded_value.write_id,
               intent_iter_.value().ToDebugHexString()));
    write_id_ = decoded_value.write_id;

    // Intents for row locks should be ignored (i.e. should not be written as regular records).
    if (decoded_value.body.starts_with(ValueEntryTypeAsChar::kRowLock)) {
      return false;
    }

    // Intents from aborted subtransactions should not be written as regular records.
    if (aborted_.Test(decoded_value.subtransaction_id)) {
      return false;
    }

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

    handler->Put(key_parts, value_parts);
    ++write_id_;
    RegisterRecord();

    YB_TRANSACTION_DUMP(
        ApplyIntent, transaction_id(), intent.doc_path.size(), intent.doc_path,
        commit_ht_, write_id_, decoded_value.body);

    RETURN_NOT_OK(UpdateSchemaVersion(intent.doc_path, decoded_value.body));
  }

  return false;
}

void ApplyIntentsContext::Complete(rocksdb::DirectWriteHandler* handler) {
  if (apply_state_) {
    char tombstone_value_type = ValueEntryTypeAsChar::kTombstone;
    std::array<Slice, 1> value_parts = {{Slice(&tombstone_value_type, 1)}};
    PutApplyState(transaction_id().AsSlice(), commit_ht_, write_id_, value_parts, handler);
  }
  FlushSchemaVersion();
}

Status FrontierSchemaVersionUpdater::UpdateSchemaVersion(Slice key, Slice value) {
  if (!frontiers_) {
    return Status::OK();
  }
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
      FlushSchemaVersion();
      schema_version_table_ = cotable_id;
    }
  } else {
    ColocationId colocation_id = 0;
    if (VERIFY_RESULT(decoder.DecodeColocationId(&colocation_id))) {
      if (colocation_id != schema_version_colocation_id_) {
        FlushSchemaVersion();
        cotable_id = VERIFY_RESULT(schema_packing_provider_->ColocationPacking(
            colocation_id, kLatestSchemaVersion, HybridTime::kMax)).cotable_id;
        DCHECK(!cotable_id.IsNil()) << cotable_id.ToString();
        schema_version_table_ = cotable_id;
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
  frontiers_->Smallest().UpdateSchemaVersion(
      schema_version_table_, min_schema_version_, rocksdb::UpdateUserValueType::kSmallest);
  frontiers_->Largest().UpdateSchemaVersion(
      schema_version_table_, max_schema_version_, rocksdb::UpdateUserValueType::kLargest);
  min_schema_version_ = std::numeric_limits<SchemaVersion>::max();
  max_schema_version_ = std::numeric_limits<SchemaVersion>::min();
}

RemoveIntentsContext::RemoveIntentsContext(const TransactionId& transaction_id, uint8_t reason)
    : IntentsWriterContext(transaction_id), reason_(reason) {
}

Result<bool> RemoveIntentsContext::Entry(
    const Slice& key, const Slice& value, bool metadata, rocksdb::DirectWriteHandler* handler) {
  if (reached_records_limit()) {
    SetApplyState(key, 0, SubtxnSet());
    return true;
  }

  handler->SingleDelete(key);
  YB_TRANSACTION_DUMP(RemoveIntent, transaction_id(), reason_, key);
  RegisterRecord();

  if (!metadata) {
    handler->SingleDelete(value);
    YB_TRANSACTION_DUMP(RemoveIntent, transaction_id(), reason_, value);
    RegisterRecord();
  }
  return false;
}

void RemoveIntentsContext::Complete(rocksdb::DirectWriteHandler* handler) {
}

ExternalIntentsBatchWriter::ExternalIntentsBatchWriter(
    std::reference_wrapper<const LWKeyValueWriteBatchPB> put_batch, HybridTime write_hybrid_time,
    HybridTime batch_hybrid_time, rocksdb::DB* intents_db, rocksdb::WriteBatch* intents_write_batch,
    SchemaPackingProvider* schema_packing_provider)
    : FrontierSchemaVersionUpdater(schema_packing_provider),
      put_batch_(put_batch),
      write_hybrid_time_(write_hybrid_time),
      batch_hybrid_time_(batch_hybrid_time),
      intents_write_batch_(intents_write_batch) {
  if (put_batch_.apply_external_transactions().size() > 0) {
    intents_db_iter_ = CreateRocksDBIterator(
        intents_db, &docdb::KeyBounds::kNoBounds, docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
        /* user_key_for_filter= */ boost::none, rocksdb::kDefaultQueryId,
        /* read_filter= */ nullptr, &intents_db_iter_upperbound_);
  }
}

bool ExternalIntentsBatchWriter::Empty() const {
  return !put_batch_.write_pairs_size() && !put_batch_.apply_external_transactions_size();
}

namespace {

Status NotEnoughBytes(size_t present, size_t required, const Slice& full) {
  return STATUS_FORMAT(
      Corruption, "Not enough bytes in external intents $0 while $1 expected, full: $2", present,
      required, full.ToDebugHexString());
}

Result<ExternalTxnApplyState> ProcessApplyExternalTransactions(
    const LWKeyValueWriteBatchPB& put_batch) {
  ExternalTxnApplyState result;
  for (const auto& apply : put_batch.apply_external_transactions()) {
    auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(apply.transaction_id()));
    auto commit_ht = HybridTime(apply.commit_hybrid_time());
    auto aborted_subtransactions =
        VERIFY_RESULT(SubtxnSet::FromPB(apply.aborted_subtransactions().set()));
    result.emplace(
        txn_id, ExternalTxnApplyStateData{
                    .commit_ht = commit_ht,
                    .aborted_subtransactions = aborted_subtransactions,
                    // If filter keys are not specified then default to full key range.
                    .filter_range = {apply.filter_start_key(), apply.filter_end_key()},
                });
  }

  return result;
}

}  // namespace

Result<bool> ExternalIntentsBatchWriter::PrepareApplyExternalIntentsBatch(
    const Slice& original_input_value, ExternalTxnApplyStateData* apply_data,
    rocksdb::DirectWriteHandler* regular_write_handler) {
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

    // Remove the key entry prefix byte(s) and verify the key is valid.
    auto output_key_value = output_key;
    auto output_key_value_byte = dockv::ConsumeKeyEntryType(&output_key_value);
    SCHECK_NE(output_key_value_byte, KeyEntryType::kInvalid, Corruption, "Wrong first byte");
    if (!apply_data->filter_range.IsWithinBounds(output_key_value)) {
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
    regular_write_handler->Put(key_parts, value_parts);
    ++apply_data->write_id;

    // Update min/max schema version.
    RETURN_NOT_OK(UpdateSchemaVersion(output_key, output_value));
  }

  return can_delete_entire_batch;
}

// Reads all stored external intents for provided transactions and prepares batches that will apply
// them into regular db and remove from intents db.
Status ExternalIntentsBatchWriter::PrepareApplyExternalIntents(
    ExternalTxnApplyState* apply_external_transactions, rocksdb::DirectWriteHandler* handler) {
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

Result<bool> ExternalIntentsBatchWriter::AddExternalPairToWriteBatch(
    const yb::docdb::LWKeyValuePairPB& kv_pair, ExternalTxnApplyState* apply_external_transactions,
    rocksdb::DirectWriteHandler* regular_write_handler, IntraTxnWriteId* write_id) {
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

Status ExternalIntentsBatchWriter::Apply(rocksdb::DirectWriteHandler* handler) {
  auto apply_external_transactions = VERIFY_RESULT(ProcessApplyExternalTransactions(put_batch_));
  if (!apply_external_transactions.empty()) {
    DCHECK(intents_db_iter_.Initialized());
    RETURN_NOT_OK(PrepareApplyExternalIntents(&apply_external_transactions, handler));
  }

  DocHybridTimeBuffer doc_ht_buffer;
  IntraTxnWriteId write_id = 0;
  for (const auto& write_pair : put_batch_.write_pairs()) {
    if (VERIFY_RESULT(AddExternalPairToWriteBatch(
            write_pair, &apply_external_transactions, handler, &write_id))) {
      HandleExternalRecord(write_pair, write_hybrid_time_, &doc_ht_buffer, handler, &write_id);

      RETURN_NOT_OK(UpdateSchemaVersion(write_pair.key(), write_pair.value()));
    }
  }

  return Status::OK();
}

}  // namespace docdb
} // namespace yb
