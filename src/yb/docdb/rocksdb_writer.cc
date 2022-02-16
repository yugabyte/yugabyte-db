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

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/walltime.h"

#include "yb/util/flag_tags.h"

DEFINE_bool(enable_transaction_sealing, false,
            "Whether transaction sealing is enabled.");
DEFINE_test_flag(bool, docdb_sort_weak_intents, false,
                "Sort weak intents to make their order deterministic.");

namespace yb {
namespace docdb {

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
  char reverse_key_prefix[1] = { ValueTypeAsChar::kTransactionId };
  DocHybridTimeWordBuffer doc_ht_buffer;
  auto doc_ht_slice = InvertEncodedDocHT(key.parts[N - 1], &doc_ht_buffer);

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

} // namespace

NonTransactionalWriter::NonTransactionalWriter(
    std::reference_wrapper<const docdb::KeyValueWriteBatchPB> put_batch, HybridTime hybrid_time)
    : put_batch_(put_batch), hybrid_time_(hybrid_time) {
}

bool NonTransactionalWriter::Empty() const {
  return put_batch_.write_pairs().empty();
}

Status NonTransactionalWriter::Apply(rocksdb::DirectWriteHandler* handler) {
  DocHybridTimeBuffer doc_ht_buffer;

  int write_id = 0;
  for (const auto& kv_pair : put_batch_.write_pairs()) {

    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());

    if (kv_pair.key()[0] == ValueTypeAsChar::kExternalTransactionId) {
      continue;
    }

#ifndef NDEBUG
    // Debug-only: ensure all keys we get in Raft replication can be decoded.
    SubDocKey subdoc_key;
    Status s = subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(kv_pair.key());
    CHECK(s.ok())
        << "Failed decoding key: " << s.ToString() << "; "
        << "Problematic key: " << BestEffortDocDBKeyToStr(KeyBytes(kv_pair.key())) << "\n"
        << "value: " << FormatBytesAsStr(kv_pair.value());
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

    auto hybrid_time = kv_pair.has_external_hybrid_time() ?
        HybridTime(kv_pair.external_hybrid_time()) : hybrid_time_;
    std::array<Slice, 2> key_parts = {{
        Slice(kv_pair.key()),
        doc_ht_buffer.EncodeWithValueType(hybrid_time, write_id),
    }};
    Slice key_value = kv_pair.value();
    handler->Put(key_parts, SliceParts(&key_value, 1));

    ++write_id;
  }

  return Status::OK();
}

TransactionalWriter::TransactionalWriter(
    std::reference_wrapper<const docdb::KeyValueWriteBatchPB> put_batch,
    HybridTime hybrid_time,
    const TransactionId& transaction_id,
    IsolationLevel isolation_level,
    PartialRangeKeyIntents partial_range_key_intents,
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
CHECKED_STATUS TransactionalWriter::Apply(rocksdb::DirectWriteHandler* handler) {
  VLOG(4) << "PrepareTransactionWriteBatch(), write_id = " << write_id_;

  row_mark_ = GetRowMarkTypeFromPB(put_batch_);
  handler_ = handler;

  if (metadata_to_store_) {
    auto txn_value_type = ValueTypeAsChar::kTransactionId;
    std::array<Slice, 2> key = {
      Slice(&txn_value_type, 1),
      transaction_id_.AsSlice(),
    };
    auto data_copy = *metadata_to_store_;
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
    strong_intent_types_ = GetStrongIntentTypeSet(
        isolation_level_, OperationKind::kWrite, row_mark_);

    // We cannot recover from failures here, because it means that we cannot apply replicated
    // operation.
    RETURN_NOT_OK(EnumerateIntents(
        put_batch_.write_pairs(), std::ref(*this), partial_range_key_intents_));
  }

  if (!put_batch_.read_pairs().empty()) {
    strong_intent_types_ = GetStrongIntentTypeSet(
        isolation_level_, OperationKind::kRead, row_mark_);
    RETURN_NOT_OK(EnumerateIntents(
        put_batch_.read_pairs(), std::ref(*this), partial_range_key_intents_));
  }

  return Finish();
}

// Using operator() to pass this object conveniently to EnumerateIntents.
CHECKED_STATUS TransactionalWriter::operator()(
    IntentStrength intent_strength, FullDocKey, Slice value_slice, KeyBytes* key,
    LastKey last_key) {
  if (intent_strength == IntentStrength::kWeak) {
    weak_intents_[key->data()] |= StrongToWeak(strong_intent_types_);
    return Status::OK();
  }

  const auto transaction_value_type = ValueTypeAsChar::kTransactionId;
  const auto write_id_value_type = ValueTypeAsChar::kWriteId;
  const auto row_lock_value_type = ValueTypeAsChar::kRowLock;
  IntraTxnWriteId big_endian_write_id = BigEndian::FromHost32(intra_txn_write_id_);

  const auto subtransaction_value_type = ValueTypeAsChar::kSubTransactionId;
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
      value_slice,
  }};
  // Store a row lock indicator rather than data (in value_slice) for row lock intents.
  if (IsValidRowMarkType(row_mark_)) {
    value.back() = Slice(&row_lock_value_type, 1);
  }

  ++intra_txn_write_id_;

  char intent_type[2] = { ValueTypeAsChar::kIntentTypeSet,
                          static_cast<char>(strong_intent_types_.ToUIntPtr()) };

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

CHECKED_STATUS TransactionalWriter::Finish() {
  char transaction_id_value_type = ValueTypeAsChar::kTransactionId;

  DocHybridTimeBuffer doc_ht_buffer;

  std::array<Slice, 2> value = {{
      Slice(&transaction_id_value_type, 1),
      transaction_id_.AsSlice(),
  }};

  if (PREDICT_FALSE(FLAGS_TEST_docdb_sort_weak_intents)) {
    // This is done in tests when deterministic DocDB state is required.
    std::vector<std::pair<KeyBuffer, IntentTypeSet>> intents_and_types(
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

CHECKED_STATUS TransactionalWriter::AddWeakIntent(
    const std::pair<KeyBuffer, IntentTypeSet>& intent_and_types,
    const std::array<Slice, 2>& value,
    DocHybridTimeBuffer* doc_ht_buffer) {
  char intent_type[2] = { ValueTypeAsChar::kIntentTypeSet,
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
  buffer_[0] = ValueTypeAsChar::kHybridTime;
}

} // namespace docdb
} // namespace yb
