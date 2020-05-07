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

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/shared_lock_manager.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"
#include "yb/docdb/deadline_info.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/kv_debug.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"

#include "yb/util/bitmap.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/metrics.h"
#include "yb/util/flag_tags.h"

#include "yb/yql/cql/ql/util/errcodes.h"

using std::endl;
using std::list;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::shared_ptr;
using std::stack;
using std::vector;
using std::make_shared;

using yb::HybridTime;
using yb::FormatBytesAsStr;
using strings::Substitute;

using namespace std::placeholders;

DEFINE_test_flag(bool, docdb_sort_weak_intents_in_tests, false,
                "Sort weak intents to make their order deterministic.");
DEFINE_bool(enable_transaction_sealing, false,
            "Whether transaction sealing is enabled.");
DEFINE_test_flag(bool, TEST_fail_on_replicated_batch_idx_set_in_txn_record, false,
                 "Fail when a set of replicated batch indexes is found in txn record.");

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
    rocksdb::WriteBatch* rocksdb_write_batch,
    Slice reverse_value_prefix = Slice()) {
  char reverse_key_prefix[1] = { ValueTypeAsChar::kTransactionId };
  size_t doc_ht_buffer[kMaxWordsPerEncodedHybridTimeWithValueType];
  auto doc_ht_slice = key.parts[N - 1];
  memcpy(doc_ht_buffer, doc_ht_slice.data(), doc_ht_slice.size());
  for (size_t i = 0; i != kMaxWordsPerEncodedHybridTimeWithValueType; ++i) {
    doc_ht_buffer[i] = ~doc_ht_buffer[i];
  }
  doc_ht_slice = Slice(pointer_cast<char*>(doc_ht_buffer), doc_ht_slice.size());

  std::array<Slice, 3> reverse_key = {{
      Slice(reverse_key_prefix, sizeof(reverse_key_prefix)),
      transaction_id.AsSlice(),
      doc_ht_slice,
  }};
  rocksdb_write_batch->Put(key, value);
  if (reverse_value_prefix.empty()) {
    rocksdb_write_batch->Put(reverse_key, key);
  } else {
    std::array<Slice, N + 1> reverse_value;
    reverse_value[0] = reverse_value_prefix;
    memcpy(&reverse_value[1], key.parts, sizeof(*key.parts) * N);
    rocksdb_write_batch->Put(reverse_key, reverse_value);
  }
}

// key should be valid prefix of doc key, ending with some complete pritimive value or group end.
CHECKED_STATUS ApplyIntent(RefCntPrefix key,
                           const IntentTypeSet intent_types,
                           LockBatchEntries *keys_locked) {
  // Have to strip kGroupEnd from end of key, because when only hash key is specified, we will
  // get two kGroupEnd at end of strong intent.
  size_t size = key.size();
  if (size > 0) {
    if (key.data()[0] == ValueTypeAsChar::kGroupEnd) {
      if (size != 1) {
        return STATUS_FORMAT(Corruption, "Key starting with group end: $0",
            key.as_slice().ToDebugHexString());
      }
      size = 0;
    } else {
      while (key.data()[size - 1] == ValueTypeAsChar::kGroupEnd) {
        --size;
      }
    }
  }
  key.Resize(size);
  keys_locked->push_back({key, intent_types});
  return Status::OK();
}

struct DetermineKeysToLockResult {
  LockBatchEntries lock_batch;
  bool need_read_snapshot;
};

Result<DetermineKeysToLockResult> DetermineKeysToLock(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const google::protobuf::RepeatedPtrField<KeyValuePairPB>& read_pairs,
    const IsolationLevel isolation_level,
    const OperationKind operation_kind,
    const RowMarkType row_mark_type,
    bool transactional_table,
    PartialRangeKeyIntents partial_range_key_intents) {
  DetermineKeysToLockResult result;
  boost::container::small_vector<RefCntPrefix, 8> doc_paths;
  boost::container::small_vector<size_t, 32> key_prefix_lengths;
  result.need_read_snapshot = false;
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    doc_paths.clear();
    IsolationLevel level;
    RETURN_NOT_OK(doc_op->GetDocPaths(GetDocPathsMode::kLock, &doc_paths, &level));
    if (isolation_level != IsolationLevel::NON_TRANSACTIONAL) {
      level = isolation_level;
    }
    IntentTypeSet strong_intent_types = GetStrongIntentTypeSet(level, operation_kind,
                                                               row_mark_type);
    if (isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION &&
        operation_kind == OperationKind::kWrite &&
        doc_op->RequireReadSnapshot()) {
      strong_intent_types = IntentTypeSet({IntentType::kStrongRead, IntentType::kStrongWrite});
    }

    for (const auto& doc_path : doc_paths) {
      key_prefix_lengths.clear();
      RETURN_NOT_OK(SubDocKey::DecodePrefixLengths(doc_path.as_slice(), &key_prefix_lengths));
      // At least entire doc_path should be returned, so empty key_prefix_lengths is an error.
      if (key_prefix_lengths.empty()) {
        return STATUS_FORMAT(Corruption, "Unable to decode key prefixes from: $0",
                             doc_path.as_slice().ToDebugHexString());
      }
      // We will acquire strong lock on entire doc_path, so remove it from list of weak locks.
      key_prefix_lengths.pop_back();
      auto partial_key = doc_path;
      // Acquire weak lock on empty key for transactional tables,
      // unless specified key is already empty.
      if (doc_path.size() > 0 && transactional_table) {
        partial_key.Resize(0);
        RETURN_NOT_OK(ApplyIntent(
            partial_key, StrongToWeak(strong_intent_types), &result.lock_batch));
      }
      for (size_t prefix_length : key_prefix_lengths) {
        partial_key.Resize(prefix_length);
        RETURN_NOT_OK(ApplyIntent(
            partial_key, StrongToWeak(strong_intent_types), &result.lock_batch));
      }

      RETURN_NOT_OK(ApplyIntent(doc_path, strong_intent_types, &result.lock_batch));
    }

    if (doc_op->RequireReadSnapshot()) {
      result.need_read_snapshot = true;
    }
  }

  if (!read_pairs.empty()) {
    RETURN_NOT_OK(EnumerateIntents(
        read_pairs,
        [&result](IntentStrength strength, Slice value, KeyBytes* key, LastKey) {
          RefCntPrefix prefix(key->data());
          auto intent_types = strength == IntentStrength::kStrong
              ? IntentTypeSet({IntentType::kStrongRead})
              : IntentTypeSet({IntentType::kWeakRead});
          return ApplyIntent(prefix, intent_types, &result.lock_batch);
        }, partial_range_key_intents));
  }

  return result;
}

void FilterKeysToLock(LockBatchEntries *keys_locked) {
  if (keys_locked->empty()) {
    return;
  }

  std::sort(keys_locked->begin(), keys_locked->end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.key < rhs.key;
            });

  auto w = keys_locked->begin();
  for (auto it = keys_locked->begin(); ++it != keys_locked->end();) {
    if (it->key == w->key) {
      w->intent_types |= it->intent_types;
    } else {
      ++w;
      *w = *it;
    }
  }

  ++w;
  keys_locked->erase(w, keys_locked->end());
}

}  // namespace

const SliceKeyBound& SliceKeyBound::Invalid() {
  static SliceKeyBound result;
  return result;
}

const IndexBound& IndexBound::Empty() {
  static IndexBound result;
  return result;
}

Result<PrepareDocWriteOperationResult> PrepareDocWriteOperation(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const google::protobuf::RepeatedPtrField<KeyValuePairPB>& read_pairs,
    const scoped_refptr<Histogram>& write_lock_latency,
    const IsolationLevel isolation_level,
    const OperationKind operation_kind,
    const RowMarkType row_mark_type,
    bool transactional_table,
    CoarseTimePoint deadline,
    PartialRangeKeyIntents partial_range_key_intents,
    SharedLockManager *lock_manager) {
  PrepareDocWriteOperationResult result;

  auto determine_keys_to_lock_result = VERIFY_RESULT(DetermineKeysToLock(
      doc_write_ops, read_pairs, isolation_level, operation_kind, row_mark_type,
      transactional_table, partial_range_key_intents));
  if (determine_keys_to_lock_result.lock_batch.empty()) {
    LOG(ERROR) << "Empty lock batch, doc_write_ops: " << yb::ToString(doc_write_ops)
               << ", read pairs: " << yb::ToString(read_pairs);
    return STATUS(Corruption, "Empty lock batch");
  }
  result.need_read_snapshot = determine_keys_to_lock_result.need_read_snapshot;

  FilterKeysToLock(&determine_keys_to_lock_result.lock_batch);
  const MonoTime start_time = (write_lock_latency != nullptr) ? MonoTime::Now() : MonoTime();
  result.lock_batch = LockBatch(
      lock_manager, std::move(determine_keys_to_lock_result.lock_batch), deadline);
  RETURN_NOT_OK_PREPEND(
      result.lock_batch.status(), Format("Timeout: $0", deadline - ToCoarse(start_time)));
  if (write_lock_latency != nullptr) {
    const MonoDelta elapsed_time = MonoTime::Now().GetDeltaSince(start_time);
    write_lock_latency->Increment(elapsed_time.ToMicroseconds());
  }

  return result;
}

Status SetDocOpQLErrorResponse(DocOperation* doc_op, string err_msg) {
  switch (doc_op->OpType()) {
    case DocOperation::Type::QL_WRITE_OPERATION: {
      const auto &resp = down_cast<QLWriteOperation *>(doc_op)->response();
      resp->set_status(QLResponsePB::YQL_STATUS_QUERY_ERROR);
      resp->set_error_message(err_msg);
      break;
    }
    case DocOperation::Type::PGSQL_WRITE_OPERATION: {
      const auto &resp = down_cast<PgsqlWriteOperation *>(doc_op)->response();
      resp->set_status(PgsqlResponsePB::PGSQL_STATUS_USAGE_ERROR);
      resp->set_error_message(err_msg);
      break;
    }
    default:
      return STATUS_FORMAT(InternalError,
                           "Invalid status (QLError) for doc operation %d",
                           doc_op->OpType());
  }
  return Status::OK();
}

Status ExecuteDocWriteOperation(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                                CoarseTimePoint deadline,
                                const ReadHybridTime& read_time,
                                const DocDB& doc_db,
                                KeyValueWriteBatchPB* write_batch,
                                InitMarkerBehavior init_marker_behavior,
                                std::atomic<int64_t>* monotonic_counter,
                                HybridTime* restart_read_ht,
                                const string& table_name) {
  DCHECK_ONLY_NOTNULL(restart_read_ht);
  DocWriteBatch doc_write_batch(doc_db, init_marker_behavior, monotonic_counter);
  DocOperationApplyData data = {&doc_write_batch, deadline, read_time, restart_read_ht};
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    Status s = doc_op->Apply(data);
    if (s.IsQLError()) {
      string error_msg;
      if (ql::GetErrorCode(s) == ql::ErrorCode::CONDITION_NOT_SATISFIED) {
        // Generating the error message here because 'table_name'
        // is not available on the lower level - in doc_op->Apply().
        error_msg = Format("Condition on table $0 was not satisfied.", table_name);
      } else {
        error_msg =  s.message().ToBuffer();
      }

      // Ensure we set appropriate error in the response object for QL errors.
      RETURN_NOT_OK(SetDocOpQLErrorResponse(doc_op.get(), error_msg));
      continue;
    }

    RETURN_NOT_OK(s);
  }
  doc_write_batch.MoveToWriteBatchPB(write_batch);
  return Status::OK();
}

void PrepareNonTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch) {
  CHECK(put_batch.read_pairs().empty());

  DocHybridTimeBuffer doc_ht_buffer;
  for (int write_id = 0; write_id < put_batch.write_pairs_size(); ++write_id) {
    const auto& kv_pair = put_batch.write_pairs(write_id);
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());

#ifndef NDEBUG
    // Debug-only: ensure all keys we get in Raft replication can be decoded.
    {
      SubDocKey subdoc_key;
      Status s = subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(kv_pair.key());
      CHECK(s.ok())
          << "Failed decoding key: " << s.ToString() << "; "
          << "Problematic key: " << BestEffortDocDBKeyToStr(KeyBytes(kv_pair.key())) << "\n"
          << "value: " << FormatBytesAsStr(kv_pair.value()) << "\n"
          << "put_batch:\n" << put_batch.DebugString();
    }
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

    hybrid_time = kv_pair.has_external_hybrid_time() ?
        HybridTime(kv_pair.external_hybrid_time()) : hybrid_time;
    std::array<Slice, 2> key_parts = {{
        Slice(kv_pair.key()),
        doc_ht_buffer.EncodeWithValueType(hybrid_time, write_id),
    }};
    Slice key_value = kv_pair.value();
    rocksdb_write_batch->Put(key_parts, { &key_value, 1 });
  }
}

namespace {

// Checks if the given slice points to the part of an encoded SubDocKey past all of the subkeys
// (and definitely past all the hash/range keys). The only remaining part could be a hybrid time.
inline bool IsEndOfSubKeys(const Slice& key) {
  return key[0] == ValueTypeAsChar::kGroupEnd &&
         (key.size() == 1 || key[1] == ValueTypeAsChar::kHybridTime);
}

// Enumerates weak intents generated by the given key by invoking the provided callback with each
// weak intent stored in encoded_key_buffer. On return, *encoded_key_buffer contains the
// corresponding strong intent, for which the callback has not yet been called. It is expected
// that the caller would do so.
Status EnumerateWeakIntents(
    Slice key,
    const EnumerateIntentsCallback& functor,
    KeyBytes* encoded_key_buffer,
    PartialRangeKeyIntents partial_range_key_intents) {
  static const Slice kEmptyIntentValue;

  encoded_key_buffer->Clear();
  if (key.empty()) {
    return STATUS(Corruption, "An empty slice is not a valid encoded SubDocKey");
  }

  const bool has_cotable_id = *key.cdata() == ValueTypeAsChar::kTableId;
  const bool has_pgtable_id = *key.cdata() == ValueTypeAsChar::kPgTableOid;
  {
    bool is_table_root_key = false;
    if (has_cotable_id) {
      const auto kMinExpectedSize = kUuidSize + 2;
      if (key.size() < kMinExpectedSize) {
        return STATUS_FORMAT(
            Corruption,
            "Expected an encoded SubDocKey starting with a cotable id to be at least $0 bytes long",
            kMinExpectedSize);
      }
      encoded_key_buffer->AppendRawBytes(key.cdata(), kUuidSize + 1);
      is_table_root_key = key[kUuidSize + 1] == ValueTypeAsChar::kGroupEnd;
    } else if (has_pgtable_id) {
      const auto kMinExpectedSize = sizeof(PgTableOid) + 2;
      if (key.size() < kMinExpectedSize) {
        return STATUS_FORMAT(
            Corruption,
            "Expected an encoded SubDocKey starting with a pgtable id to be at least $0 bytes long",
            kMinExpectedSize);
      }
      encoded_key_buffer->AppendRawBytes(key.cdata(), sizeof(PgTableOid) + 1);
      is_table_root_key = key[sizeof(PgTableOid) + 1] == ValueTypeAsChar::kGroupEnd;
    } else {
      is_table_root_key = *key.cdata() == ValueTypeAsChar::kGroupEnd;
    }

    encoded_key_buffer->AppendValueType(ValueType::kGroupEnd);

    if (is_table_root_key) {
      // This must be a "table root" (or "tablet root") key (no hash components, no range
      // components, but the cotable might still be there). We are not really considering the case
      // of any subkeys under the empty key, so we can return here.
      return Status::OK();
    }
  }

  // For any non-empty key we already know that the empty key intent is weak.
  RETURN_NOT_OK(functor(
      IntentStrength::kWeak, kEmptyIntentValue, encoded_key_buffer, LastKey::kFalse));

  auto hashed_part_size = VERIFY_RESULT(DocKey::EncodedSize(key, DocKeyPart::UP_TO_HASH));

  // Remove kGroupEnd that we just added to generate a weak intent.
  encoded_key_buffer->RemoveLastByte();

  if (hashed_part_size != encoded_key_buffer->size()) {
    // A hash component is present. Note that if cotable id is present, hashed_part_size would
    // also include it, so we only need to append the new bytes.
    encoded_key_buffer->AppendRawBytes(
        key.cdata() + encoded_key_buffer->size(), hashed_part_size - encoded_key_buffer->size());
    key.remove_prefix(hashed_part_size);
    if (key.empty()) {
      return STATUS(Corruption, "Range key part missing, expected at least a kGroupEnd");
    }

    // Append the kGroupEnd at the end for the empty range part to make this a valid encoded DocKey.
    encoded_key_buffer->AppendValueType(ValueType::kGroupEnd);
    if (IsEndOfSubKeys(key)) {
      // This means the key ends at the hash component -- no range keys and no subkeys.
      return Status::OK();
    }

    // Generate a week intent that only includes the hash component.
    RETURN_NOT_OK(functor(
        IntentStrength::kWeak, kEmptyIntentValue, encoded_key_buffer, LastKey::kFalse));

    // Remove the kGroupEnd we added a bit earlier so we can append some range components.
    encoded_key_buffer->RemoveLastByte();
  } else {
    // No hash component.
    key.remove_prefix(hashed_part_size);
  }

  // Range components.
  auto range_key_start = key.cdata();
  while (VERIFY_RESULT(ConsumePrimitiveValueFromKey(&key))) {
    encoded_key_buffer->AppendRawBytes(range_key_start, key.cdata() - range_key_start);
    // We always need kGroupEnd at the end to make this a valid encoded DocKey.
    encoded_key_buffer->AppendValueType(ValueType::kGroupEnd);
    if (key.empty()) {
      return STATUS(Corruption, "Range key part is not terminated with a kGroupEnd");
    }
    if (IsEndOfSubKeys(key)) {
      // This is the last range key and there are no subkeys.
      return Status::OK();
    }
    if (partial_range_key_intents || *key.cdata() == ValueTypeAsChar::kGroupEnd) {
      RETURN_NOT_OK(functor(
          IntentStrength::kWeak, kEmptyIntentValue, encoded_key_buffer, LastKey::kFalse));
    }
    encoded_key_buffer->RemoveLastByte();
    range_key_start = key.cdata();
  }

  // We still need to append the kGroupEnd byte that closes the range portion to our buffer.
  // The corresponding kGroupEnd has already been consumed from the key slice by the last call to
  // ConsumePrimitiveValueFromKey, which returned false.
  encoded_key_buffer->AppendValueType(ValueType::kGroupEnd);

  auto subkey_start = key.cdata();
  while (VERIFY_RESULT(SubDocKey::DecodeSubkey(&key))) {
    encoded_key_buffer->AppendRawBytes(subkey_start, key.cdata() - subkey_start);
    if (key.empty() || *key.cdata() == ValueTypeAsChar::kHybridTime) {
      // This was the last subkey.
      return Status::OK();
    }
    RETURN_NOT_OK(functor(
        IntentStrength::kWeak, kEmptyIntentValue, encoded_key_buffer, LastKey::kFalse));
    subkey_start = key.cdata();
  }

  return STATUS(
      Corruption,
      "Expected to reach the end of the key after decoding last valid subkey");
}

}  // anonymous namespace

Status EnumerateIntents(
    Slice key, const Slice& intent_value, const EnumerateIntentsCallback& functor,
    KeyBytes* encoded_key_buffer, PartialRangeKeyIntents partial_range_key_intents,
    LastKey last_key) {
  RETURN_NOT_OK(EnumerateWeakIntents(
      key, functor, encoded_key_buffer, partial_range_key_intents));
  return functor(IntentStrength::kStrong, intent_value, encoded_key_buffer, last_key);
}

Status EnumerateIntents(
    const google::protobuf::RepeatedPtrField<KeyValuePairPB> &kv_pairs,
    const EnumerateIntentsCallback& functor, PartialRangeKeyIntents partial_range_key_intents) {
  KeyBytes encoded_key;

  for (int index = 0; index < kv_pairs.size(); ) {
    const auto &kv_pair = kv_pairs.Get(index);
    ++index;
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());
    RETURN_NOT_OK(EnumerateIntents(
        kv_pair.key(), kv_pair.value(), functor, &encoded_key, partial_range_key_intents,
        LastKey(index == kv_pairs.size())));
  }

  return Status::OK();
}

class PrepareTransactionWriteBatchHelper {
 public:
  PrepareTransactionWriteBatchHelper(const PrepareTransactionWriteBatchHelper&) = delete;
  void operator=(const PrepareTransactionWriteBatchHelper&) = delete;

  // `rocksdb_write_batch` - in-out parameter is filled by this prepare.
  PrepareTransactionWriteBatchHelper(HybridTime hybrid_time,
                                     rocksdb::WriteBatch* rocksdb_write_batch,
                                     const TransactionId& transaction_id,
                                     const Slice& replicated_batches_state,
                                     IntraTxnWriteId* intra_txn_write_id)
      : hybrid_time_(hybrid_time),
        rocksdb_write_batch_(rocksdb_write_batch),
        transaction_id_(transaction_id),
        replicated_batches_state_(replicated_batches_state),
        intra_txn_write_id_(intra_txn_write_id) {
  }

  void Setup(
      IsolationLevel isolation_level,
      OperationKind kind,
      RowMarkType row_mark) {
    row_mark_ = row_mark;
    strong_intent_types_ = GetStrongIntentTypeSet(isolation_level, kind, row_mark);
  }

  // Using operator() to pass this object conveniently to EnumerateIntents.
  CHECKED_STATUS operator()(IntentStrength intent_strength, Slice value_slice, KeyBytes* key,
                            LastKey last_key) {
    if (intent_strength == IntentStrength::kWeak) {
      weak_intents_[key->data()] |= StrongToWeak(strong_intent_types_);
      return Status::OK();
    }

    const auto transaction_value_type = ValueTypeAsChar::kTransactionId;
    const auto write_id_value_type = ValueTypeAsChar::kWriteId;
    const auto row_lock_value_type = ValueTypeAsChar::kRowLock;
    IntraTxnWriteId big_endian_write_id = BigEndian::FromHost32(*intra_txn_write_id_);
    std::array<Slice, 5> value = {{
        Slice(&transaction_value_type, 1),
        transaction_id_.AsSlice(),
        Slice(&write_id_value_type, 1),
        Slice(pointer_cast<char*>(&big_endian_write_id), sizeof(big_endian_write_id)),
        value_slice,
    }};
    // Store a row lock indicator rather than data (in value_slice) for row lock intents.
    if (IsValidRowMarkType(row_mark_)) {
      value.back() = Slice(&row_lock_value_type, 1);
    }

    ++*intra_txn_write_id_;

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
    AddIntent<kNumKeyParts>(
        transaction_id_, key_parts, value, rocksdb_write_batch_, reverse_value_prefix);

    return Status::OK();
  }

  void Finish() {
    char transaction_id_value_type = ValueTypeAsChar::kTransactionId;

    DocHybridTimeBuffer doc_ht_buffer;

    std::array<Slice, 2> value = {{
        Slice(&transaction_id_value_type, 1),
        transaction_id_.AsSlice(),
    }};

    if (PREDICT_TRUE(!FLAGS_docdb_sort_weak_intents_in_tests)) {
      for (const auto& intent_and_types : weak_intents_) {
        AddWeakIntent(intent_and_types, value, &doc_ht_buffer);
      }
    } else {
      // This is done in tests when deterministic DocDB state is required.
      std::vector<std::pair<std::string, IntentTypeSet>> intents_and_types(
          weak_intents_.begin(), weak_intents_.end());
      sort(intents_and_types.begin(), intents_and_types.end());
      for (const auto& intent_and_types : intents_and_types) {
        AddWeakIntent(intent_and_types, value, &doc_ht_buffer);
      }
    }
  }

 private:
  void AddWeakIntent(
      const std::pair<std::string, IntentTypeSet>& intent_and_types,
      const std::array<Slice, 2>& value,
      DocHybridTimeBuffer* doc_ht_buffer) {
    char intent_type[2] = { ValueTypeAsChar::kIntentTypeSet,
                            static_cast<char>(intent_and_types.second.ToUIntPtr()) };
    constexpr size_t kNumKeyParts = 3;
    std::array<Slice, kNumKeyParts> key = {{
        Slice(intent_and_types.first),
        Slice(intent_type, 2),
        doc_ht_buffer->EncodeWithValueType(hybrid_time_, write_id_++),
    }};

    AddIntent<kNumKeyParts>(transaction_id_, key, value, rocksdb_write_batch_);
  }

  // TODO(dtxn) weak & strong intent in one batch.
  // TODO(dtxn) extract part of code knowning about intents structure to lower level.
  RowMarkType row_mark_;
  HybridTime hybrid_time_;
  rocksdb::WriteBatch* rocksdb_write_batch_;
  const TransactionId& transaction_id_;
  Slice replicated_batches_state_;
  IntentTypeSet strong_intent_types_;
  std::unordered_map<std::string, IntentTypeSet> weak_intents_;
  IntraTxnWriteId write_id_ = 0;
  IntraTxnWriteId* intra_txn_write_id_;
};

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
void PrepareTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch,
    const TransactionId& transaction_id,
    IsolationLevel isolation_level,
    PartialRangeKeyIntents partial_range_key_intents,
    const Slice& replicated_batches_state,
    IntraTxnWriteId* write_id) {
  VLOG(4) << "PrepareTransactionWriteBatch(), write_id = " << *write_id;

  RowMarkType row_mark = GetRowMarkTypeFromPB(put_batch);

  PrepareTransactionWriteBatchHelper helper(
      hybrid_time, rocksdb_write_batch, transaction_id, replicated_batches_state, write_id);

  if (!put_batch.write_pairs().empty()) {
    if (IsValidRowMarkType(row_mark)) {
      LOG(WARNING) << "Performing a write with row lock "
                   << RowMarkType_Name(row_mark)
                   << " when only reads are expected";
    }
    helper.Setup(isolation_level, OperationKind::kWrite, row_mark);

    // We cannot recover from failures here, because it means that we cannot apply replicated
    // operation.
    CHECK_OK(EnumerateIntents(put_batch.write_pairs(), std::ref(helper),
             partial_range_key_intents));
  }

  if (!put_batch.read_pairs().empty()) {
    helper.Setup(isolation_level, OperationKind::kRead, row_mark);
    CHECK_OK(EnumerateIntents(put_batch.read_pairs(), std::ref(helper), partial_range_key_intents));
  }

  helper.Finish();
}

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

void SeekToLowerBound(const SliceKeyBound& lower_bound, IntentAwareIterator* iter) {
  if (lower_bound.is_exclusive()) {
    iter->SeekPastSubKey(lower_bound.key());
  } else {
    iter->SeekForward(lower_bound.key());
  }
}

// This function does not assume that object init_markers are present. If no init marker is present,
// or if a tombstone is found at some level, it still looks for subkeys inside it if they have
// larger timestamps.
//
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts.
//
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix. Although if high_subkey is specified, the iterator is only guaranteed
// to be positioned after the high_subkey and not necessarily outside the subdocument_key prefix.
// num_values_observed is used for queries on indices, and keeps track of the number of primitive
// values observed thus far. In a query with lower index bound k, ignore the first k primitive
// values before building the subdocument.
CHECKED_STATUS BuildSubDocument(
    IntentAwareIterator* iter,
    const GetSubDocumentData& data,
    DocHybridTime low_ts,
    int64* num_values_observed) {
  VLOG(3) << "BuildSubDocument data: " << data << " read_time: " << iter->read_time()
          << " low_ts: " << low_ts;
  while (iter->valid()) {
    if (data.deadline_info && data.deadline_info->CheckAndSetDeadlinePassed()) {
      return STATUS(Expired, "Deadline for query passed.");
    }
    // Since we modify num_values_observed on recursive calls, we keep a local copy of the value.
    int64 current_values_observed = *num_values_observed;
    auto key_data = VERIFY_RESULT(iter->FetchKey());
    auto key = key_data.key;
    const auto write_time = key_data.write_time;
    VLOG(4) << "iter: " << SubDocKey::DebugSliceToString(key)
            << ", key: " << SubDocKey::DebugSliceToString(data.subdocument_key);
    DCHECK(key.starts_with(data.subdocument_key))
        << "iter: " << SubDocKey::DebugSliceToString(key)
        << ", key: " << SubDocKey::DebugSliceToString(data.subdocument_key);

    // Key could be invalidated because we could move iterator, so back it up.
    KeyBytes key_copy(key);
    key = key_copy.AsSlice();
    rocksdb::Slice value = iter->value();
    // Checking that IntentAwareIterator returns an entry with correct time.
    DCHECK(key_data.same_transaction ||
           iter->read_time().global_limit >= write_time.hybrid_time())
        << "Bad key: " << SubDocKey::DebugSliceToString(key)
        << ", global limit: " << iter->read_time().global_limit
        << ", write time: " << write_time.hybrid_time();

    if (low_ts > write_time) {
      VLOG(3) << "SeekPastSubKey: " << SubDocKey::DebugSliceToString(key);
      iter->SeekPastSubKey(key);
      continue;
    }
    Value doc_value;
    RETURN_NOT_OK(doc_value.Decode(value));
    ValueType value_type = doc_value.value_type();
    if (key == data.subdocument_key) {
      if (write_time == DocHybridTime::kMin)
        return STATUS(Corruption, "No hybrid timestamp found on entry");

      // We may need to update the TTL in individual columns.
      if (write_time.hybrid_time() >= data.exp.write_ht) {
        // We want to keep the default TTL otherwise.
        if (doc_value.ttl() != Value::kMaxTtl) {
          data.exp.write_ht = write_time.hybrid_time();
          data.exp.ttl = doc_value.ttl();
        } else if (data.exp.ttl.IsNegative()) {
          data.exp.ttl = -data.exp.ttl;
        }
      }

      // If the hybrid time is kMin, then we must be using default TTL.
      if (data.exp.write_ht == HybridTime::kMin) {
        data.exp.write_ht = write_time.hybrid_time();
      }

      bool has_expired;
      CHECK_OK(HasExpiredTTL(data.exp.write_ht, data.exp.ttl,
                             iter->read_time().read, &has_expired));

      // Treat an expired value as a tombstone written at the same time as the original value.
      if (has_expired) {
        doc_value = Value::Tombstone();
        value_type = ValueType::kTombstone;
      }

      const bool is_collection = IsCollectionType(value_type);
      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (is_collection || value_type == ValueType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (is_collection) {
          *data.result = SubDocument(value_type);
        }

        // If the subkey lower bound filters out the key we found, we want to skip to the lower
        // bound. If it does not, we want to seek to the next key. This prevents an infinite loop
        // where the iterator keeps seeking to itself if the key we found matches the low subkey.
        // TODO: why are not we doing this for arrays?
        if (IsObjectType(value_type) && !data.low_subkey->CanInclude(key)) {
          // Try to seek to the low_subkey for efficiency.
          SeekToLowerBound(*data.low_subkey, iter);
        } else {
          VLOG(3) << "SeekPastSubKey: " << SubDocKey::DebugSliceToString(key);
          iter->SeekPastSubKey(key);
        }
        continue;
      } else {
        if (!IsPrimitiveValueType(value_type)) {
          return STATUS_FORMAT(Corruption,
              "Expected primitive value type, got $0", value_type);
        }
        // TODO: the ttl_seconds in primitive value is currently only in use for CQL. At some
        // point streamline by refactoring CQL to use the mutable Expiration in GetSubDocumentData.
        if (data.exp.ttl == Value::kMaxTtl) {
          doc_value.mutable_primitive_value()->SetTtl(-1);
        } else {
          int64_t time_since_write_seconds = (
              server::HybridClock::GetPhysicalValueMicros(iter->read_time().read) -
              server::HybridClock::GetPhysicalValueMicros(write_time.hybrid_time())) /
              MonoTime::kMicrosecondsPerSecond;
          int64_t ttl_seconds = std::max(static_cast<int64_t>(0),
              data.exp.ttl.ToMilliseconds() /
              MonoTime::kMillisecondsPerSecond - time_since_write_seconds);
          doc_value.mutable_primitive_value()->SetTtl(ttl_seconds);
        }
        // Choose the user supplied timestamp if present.
        const UserTimeMicros user_timestamp = doc_value.user_timestamp();
        doc_value.mutable_primitive_value()->SetWriteTime(
            user_timestamp == Value::kInvalidUserTimestamp
            ? write_time.hybrid_time().GetPhysicalValueMicros()
            : doc_value.user_timestamp());
        if (!data.high_index->CanInclude(current_values_observed)) {
          iter->SeekOutOfSubDoc(&key_copy);
          return Status::OK();
        }
        if (data.low_index->CanInclude(*num_values_observed)) {
          *data.result = SubDocument(doc_value.primitive_value());
        }
        (*num_values_observed)++;
        VLOG(3) << "SeekOutOfSubDoc: " << SubDocKey::DebugSliceToString(key);
        iter->SeekOutOfSubDoc(&key_copy);
        return Status::OK();
      }
    }
    SubDocument descendant{PrimitiveValue(ValueType::kInvalid)};
    // TODO: what if the key we found is the same as before?
    //       We'll get into an infinite recursion then.
    {
      IntentAwareIteratorPrefixScope prefix_scope(key, iter);
      RETURN_NOT_OK(BuildSubDocument(
          iter, data.Adjusted(key, &descendant), low_ts,
          num_values_observed));

    }
    if (descendant.value_type() == ValueType::kInvalid) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }

    if (!data.low_subkey->CanInclude(key)) {
      VLOG(3) << "Filtered by low_subkey: " << data.low_subkey->ToString()
              << ", key: " << SubDocKey::DebugSliceToString(key);
      // The value provided is lower than what we are looking for, seek to the lower bound.
      SeekToLowerBound(*data.low_subkey, iter);
      continue;
    }

    // We use num_values_observed as a conservative figure for lower bound and
    // current_values_observed for upper bound so we don't lose any data we should be including.
    if (!data.low_index->CanInclude(*num_values_observed)) {
      continue;
    }

    if (!data.high_subkey->CanInclude(key)) {
      VLOG(3) << "Filtered by high_subkey: " << data.high_subkey->ToString()
              << ", key: " << SubDocKey::DebugSliceToString(key);
      // We have encountered a subkey higher than our constraints, we should stop here.
      return Status::OK();
    }

    if (!data.high_index->CanInclude(current_values_observed)) {
      return Status::OK();
    }

    if (!IsObjectType(data.result->value_type())) {
      *data.result = SubDocument();
    }

    SubDocument* current = data.result;
    size_t num_children;
    RETURN_NOT_OK(current->NumChildren(&num_children));
    if (data.limit != 0 && num_children >= data.limit) {
      // We have processed enough records.
      return Status::OK();
    }

    if (data.count_only) {
      // We need to only count the records that we found.
      data.record_count++;
    } else {
      Slice temp = key;
      temp.remove_prefix(data.subdocument_key.size());
      for (;;) {
        PrimitiveValue child;
        RETURN_NOT_OK(child.DecodeFromKey(&temp));
        if (temp.empty()) {
          current->SetChild(child, std::move(descendant));
          break;
        }
        current = current->GetOrAddChild(child).first;
      }
    }
  }

  return Status::OK();
}

// If there is a key equal to key_bytes_without_ht + some timestamp, which is later than
// max_overwrite_time, we update max_overwrite_time, and result_value (unless it is nullptr).
// If there is a TTL with write time later than the write time in expiration, it is updated with
// the new write time and TTL, unless its value is kMaxTTL.
// When the TTL found is kMaxTTL and it is not a merge record, then it is assumed not to be
// explicitly set. Because it does not override the default table ttl, exp, which was initialized
// to the table ttl, is not updated.
// Observe that exp updates based on the first record found, while max_overwrite_time updates
// based on the first non-merge record found.
// This should not be used for leaf nodes. - Why? Looks like it is already used for leaf nodes
// also.
// Note: it is responsibility of caller to make sure key_bytes_without_ht doesn't have hybrid
// time.
// TODO: We could also check that the value is kTombStone or kObject type for sanity checking - ?
// It could be a simple value as well, not necessarily kTombstone or kObject.
Status FindLastWriteTime(
    IntentAwareIterator* iter,
    const Slice& key_without_ht,
    DocHybridTime* max_overwrite_time,
    Expiration* exp,
    Value* result_value = nullptr) {
  Slice value;
  DocHybridTime doc_ht = *max_overwrite_time;
  RETURN_NOT_OK(iter->FindLatestRecord(key_without_ht, &doc_ht, &value));
  if (!iter->valid()) {
    return Status::OK();
  }

  uint64_t merge_flags = 0;
  MonoDelta ttl;
  ValueType value_type;
  RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type, &merge_flags, &ttl));
  if (value_type == ValueType::kInvalid) {
    return Status::OK();
  }

  // We update the expiration if and only if the write time is later than the write time
  // currently stored in expiration, and the record is not a regular record with default TTL.
  // This is done independently of whether the row is a TTL row.
  // In the case that the always_override flag is true, default TTL will not be preserved.
  Expiration new_exp = *exp;
  if (doc_ht.hybrid_time() >= exp->write_ht) {
    // We want to keep the default TTL otherwise.
    if (ttl != Value::kMaxTtl || merge_flags == Value::kTtlFlag || exp->always_override) {
      new_exp.write_ht = doc_ht.hybrid_time();
      new_exp.ttl = ttl;
    } else if (exp->ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If we encounter a TTL row, we assign max_overwrite_time to be the write time of the
  // original value/init marker.
  if (merge_flags == Value::kTtlFlag) {
    DocHybridTime new_ht;
    RETURN_NOT_OK(iter->NextFullValue(&new_ht, &value));

    // There could be a case where the TTL row exists, but the value has been
    // compacted away. Then, it is treated as a Tombstone written at the time
    // of the TTL row.
    if (!iter->valid() && !new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    } else {
      ValueType value_type;
      RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type));
      // Because we still do not know whether we are seeking something expired,
      // we must take the max_overwrite_time as if the value were not expired.
      doc_ht = new_ht;
    }
  }

  if ((value_type == ValueType::kTombstone || value_type == ValueType::kInvalid) &&
      !new_exp.ttl.IsNegative()) {
    new_exp.ttl = -new_exp.ttl;
  }
  *exp = new_exp;

  if (doc_ht > *max_overwrite_time) {
    *max_overwrite_time = doc_ht;
    VLOG(4) << "Max overwritten time for " << key_without_ht.ToDebugHexString() << ": "
            << *max_overwrite_time;
  }

  if (result_value)
    RETURN_NOT_OK(result_value->Decode(value));

  return Status::OK();
}

}  // namespace

yb::Status GetSubDocument(
    const DocDB& doc_db,
    const GetSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterMode::USE_BLOOM_FILTER, data.subdocument_key, query_id,
      txn_op_context, deadline, read_time);
  return GetSubDocument(iter.get(), data, nullptr /* projection */, SeekFwdSuffices::kFalse);
}

yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const GetSubDocumentData& data,
    const std::vector<PrimitiveValue>* projection,
    const SeekFwdSuffices seek_fwd_suffices) {
  // TODO(dtxn) scan through all involved first transactions to cache statuses in a batch,
  // so during building subdocument we don't need to request them one by one.
  // TODO(dtxn) we need to restart read with scan_ht = commit_ht if some transaction was committed
  // at time commit_ht within [scan_ht; read_request_time + max_clock_skew). Also we need
  // to wait until time scan_ht = commit_ht passed.
  // TODO(dtxn) for each scanned key (and its subkeys) we need to avoid new values commits at
  // ht <= scan_ht (or just ht < scan_ht?)
  // Question: what will break if we allow later commit at ht <= scan_ht ? Need to write down
  // detailed example.
  *data.doc_found = false;
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", data.subdocument_key.ToDebugHexString(),
                  db_iter->read_time().ToString());

  // The latest time at which any prefix of the given key was overwritten.
  DocHybridTime max_overwrite_ht(DocHybridTime::kMin);
  VLOG(4) << "GetSubDocument(" << data << ")";

  SubDocKey found_subdoc_key;
  auto dockey_size =
      VERIFY_RESULT(DocKey::EncodedSize(data.subdocument_key, DocKeyPart::WHOLE_DOC_KEY));

  Slice key_slice(data.subdocument_key.data(), dockey_size);

  // Check ancestors for init markers, tombstones, and expiration, tracking the expiration and
  // corresponding most recent write time in exp, and the general most recent overwrite time in
  // max_overwrite_ht.
  //
  // First, check for an ancestor at the ID level: a table tombstone.  Currently, this is only
  // supported for YSQL colocated tables.  Since iterators only ever pertain to one table, there is
  // no need to create a prefix scope here.
  if (data.table_tombstone_time && *data.table_tombstone_time == DocHybridTime::kInvalid) {
    // Only check for table tombstones if the table is colocated, as signified by the prefix of
    // kPgTableOid.
    // TODO: adjust when fixing issue #3551
    if (key_slice[0] == ValueTypeAsChar::kPgTableOid) {
      // Seek to the ID level to look for a table tombstone.  Since this seek is expensive, cache
      // the result in data.table_tombstone_time to avoid double seeking for the lifetime of the
      // DocRowwiseIterator.
      DocKey empty_key;
      RETURN_NOT_OK(empty_key.DecodeFrom(key_slice, DocKeyPart::UP_TO_ID));
      db_iter->Seek(empty_key);
      Value doc_value = Value(PrimitiveValue(ValueType::kInvalid));
      RETURN_NOT_OK(FindLastWriteTime(
          db_iter,
          empty_key.Encode(),
          &max_overwrite_ht,
          &data.exp,
          &doc_value));
      if (doc_value.value_type() == ValueType::kTombstone) {
        SCHECK_NE(max_overwrite_ht, DocHybridTime::kInvalid, Corruption,
                  "Invalid hybrid time for table tombstone");
        *data.table_tombstone_time = max_overwrite_ht;
      } else {
        *data.table_tombstone_time = DocHybridTime::kMin;
      }
    } else {
      *data.table_tombstone_time = DocHybridTime::kMin;
    }
  } else if (data.table_tombstone_time) {
    // Use the cached result.  Don't worry about exp as YSQL does not support TTL, yet.
    max_overwrite_ht = *data.table_tombstone_time;
  }
  // Second, check the descendants of the ID level.
  IntentAwareIteratorPrefixScope prefix_scope(key_slice, db_iter);
  if (seek_fwd_suffices) {
    db_iter->SeekForward(key_slice);
  } else {
    db_iter->Seek(key_slice);
  }
  {
    auto temp_key = data.subdocument_key;
    temp_key.remove_prefix(dockey_size);
    for (;;) {
      auto decode_result = VERIFY_RESULT(SubDocKey::DecodeSubkey(&temp_key));
      if (!decode_result) {
        break;
      }
      RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp));
      key_slice = Slice(key_slice.data(), temp_key.data() - key_slice.data());
    }
  }

  // By this point, key_slice is the DocKey and all the subkeys of subdocument_key. Check for
  // init-marker / tombstones at the top level; update max_overwrite_ht.
  Value doc_value = Value(PrimitiveValue(ValueType::kInvalid));
  RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp, &doc_value));

  const ValueType value_type = doc_value.value_type();

  if (data.return_type_only) {
    *data.doc_found = value_type != ValueType::kInvalid &&
      !data.exp.ttl.IsNegative();
    // Check for expiration.
    if (*data.doc_found && max_overwrite_ht != DocHybridTime::kMin) {
      bool has_expired;
      CHECK_OK(HasExpiredTTL(data.exp.write_ht, data.exp.ttl,
                             db_iter->read_time().read, &has_expired));
      *data.doc_found = !has_expired;
    }
    if (*data.doc_found) {
      // Observe that this will have the right type but not necessarily the right value.
      *data.result = SubDocument(doc_value.primitive_value());
    }
    return Status::OK();
  }

  if (projection == nullptr) {
    *data.result = SubDocument(ValueType::kInvalid);
    int64 num_values_observed = 0;
    IntentAwareIteratorPrefixScope prefix_scope(key_slice, db_iter);
    RETURN_NOT_OK(BuildSubDocument(db_iter, data, max_overwrite_ht,
                                   &num_values_observed));
    *data.doc_found = data.result->value_type() != ValueType::kInvalid;
    if (*data.doc_found) {
      if (value_type == ValueType::kRedisSet) {
        RETURN_NOT_OK(data.result->ConvertToRedisSet());
      } else if (value_type == ValueType::kRedisTS) {
        RETURN_NOT_OK(data.result->ConvertToRedisTS());
      } else if (value_type == ValueType::kRedisSortedSet) {
        RETURN_NOT_OK(data.result->ConvertToRedisSortedSet());
      } else if (value_type == ValueType::kRedisList) {
        RETURN_NOT_OK(data.result->ConvertToRedisList());
      }
    }
    return Status::OK();
  }
  // Seed key_bytes with the subdocument key. For each subkey in the projection, build subdocument
  // and reuse key_bytes while appending the subkey.
  *data.result = SubDocument();
  KeyBytes key_bytes;
  // Preallocate some extra space to avoid allocation for small subkeys.
  key_bytes.Reserve(data.subdocument_key.size() + kMaxBytesPerEncodedHybridTime + 32);
  key_bytes.AppendRawBytes(data.subdocument_key);
  const size_t subdocument_key_size = key_bytes.size();
  for (const PrimitiveValue& subkey : *projection) {
    // Append subkey to subdocument key. Reserve extra kMaxBytesPerEncodedHybridTime + 1 bytes in
    // key_bytes to avoid the internal buffer from getting reallocated and moved by SeekForward()
    // appending the hybrid time, thereby invalidating the buffer pointer saved by prefix_scope.
    subkey.AppendToKey(&key_bytes);
    key_bytes.Reserve(key_bytes.size() + kMaxBytesPerEncodedHybridTime + 1);
    // This seek is to initialize the iterator for BuildSubDocument call.
    IntentAwareIteratorPrefixScope prefix_scope(key_bytes, db_iter);
    db_iter->SeekForward(&key_bytes);
    SubDocument descendant(ValueType::kInvalid);
    int64 num_values_observed = 0;
    RETURN_NOT_OK(BuildSubDocument(
        db_iter, data.Adjusted(key_bytes, &descendant), max_overwrite_ht,
        &num_values_observed));
    *data.doc_found = descendant.value_type() != ValueType::kInvalid;
    data.result->SetChild(subkey, std::move(descendant));

    // Restore subdocument key by truncating the appended subkey.
    key_bytes.Truncate(subdocument_key_size);
  }
  // Make sure the iterator is placed outside the whole document in the end.
  key_bytes.Truncate(dockey_size);
  db_iter->SeekOutOfSubDoc(&key_bytes);
  return Status::OK();
}

// Note: Do not use if also retrieving other value, as some work will be repeated.
// Assumes every value has a TTL, and the TTL is stored in the row with this key.
// Also observe that tombstone checking only works because we assume the key has
// no ancestors.
yb::Status GetTtl(const Slice& encoded_subdoc_key,
                  IntentAwareIterator* iter,
                  bool* doc_found,
                  Expiration* exp) {
  auto dockey_size =
    VERIFY_RESULT(DocKey::EncodedSize(encoded_subdoc_key, DocKeyPart::WHOLE_DOC_KEY));
  Slice key_slice(encoded_subdoc_key.data(), dockey_size);
  iter->Seek(key_slice);
  if (!iter->valid())
    return Status::OK();
  auto key_data = VERIFY_RESULT(iter->FetchKey());
  if ((*doc_found = (!key_data.key.compare(key_slice)))) {
    Value doc_value = Value(PrimitiveValue(ValueType::kInvalid));
    RETURN_NOT_OK(doc_value.Decode(iter->value()));
    if (doc_value.value_type() == ValueType::kTombstone) {
      *doc_found = false;
    } else {
      exp->ttl = doc_value.ttl();
      exp->write_ht = key_data.write_time.hybrid_time();
    }
  }
  return Status::OK();
}

template <class DumpStringFunc>
void ProcessDumpEntry(
    Slice key, Slice value, IncludeBinary include_binary, StorageDbType db_type,
    DumpStringFunc func) {
  KeyType key_type;
  Result<std::string> key_str = DocDBKeyToDebugStr(key, db_type, &key_type);
  if (!key_str.ok()) {
    func(key_str.status().ToString());
    return;
  }
  Result<std::string> value_str = DocDBValueToDebugStr(key_type, *key_str, value);
  if (!value_str.ok()) {
    func(value_str.status().CloneAndAppend(Substitute(". Key: $0", *key_str)).ToString());
  } else {
    func(Format("$0 -> $1", *key_str, *value_str));
  }
  if (include_binary) {
    func(Format("$0 -> $1\n", FormatSliceAsStr(key), FormatSliceAsStr(value)));
  }
}

void AppendLineToStream(
    const std::string& s, ostream* out, const DocDbDumpLineFilter& filter) {
  if (filter.empty() || filter(s)) {
    *out << s << std::endl;
  }
}

void AppendToContainer(const std::string& s, std::unordered_set<std::string>* out) {
  out->insert(s);
}

void AppendToContainer(const std::string& s, std::vector<std::string>* out) {
  out->push_back(s);
}

std::string EntryToString(const rocksdb::Iterator& iterator, StorageDbType db_type) {
  std::ostringstream out;
  ProcessDumpEntry(
      iterator.key(), iterator.value(), IncludeBinary::kFalse, db_type,
      std::bind(&AppendLineToStream, _1, &out, DocDbDumpLineFilter()));
  return out.str();
}

template <class DumpStringFunc>
void DocDBDebugDump(rocksdb::DB* rocksdb, StorageDbType db_type, IncludeBinary include_binary,
    DumpStringFunc dump_func) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();

  while (iter->Valid()) {
    ProcessDumpEntry(iter->key(), iter->value(), include_binary, db_type, dump_func);
    iter->Next();
  }
}

void DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out, StorageDbType db_type,
                    IncludeBinary include_binary, const DocDbDumpLineFilter& line_filter) {
  DocDBDebugDump(
      rocksdb, db_type, include_binary,
      std::bind(&AppendLineToStream, _1, &out, line_filter));
}

std::string DocDBDebugDumpToStr(
    DocDB docdb, IncludeBinary include_binary, const DocDbDumpLineFilter& line_filter) {
  stringstream ss;
  DocDBDebugDump(docdb.regular, ss, StorageDbType::kRegular, include_binary, line_filter);
  if (docdb.intents) {
    DocDBDebugDump(docdb.intents, ss, StorageDbType::kIntents, include_binary, line_filter);
  }
  return ss.str();
}

std::string DocDBDebugDumpToStr(
    rocksdb::DB* rocksdb, StorageDbType db_type,
    IncludeBinary include_binary, const DocDbDumpLineFilter& line_filter) {
  stringstream ss;
  DocDBDebugDump(rocksdb, ss, db_type, include_binary, line_filter);
  return ss.str();
}

template <class T>
void DocDBDebugDumpToContainer(rocksdb::DB* rocksdb, T* out, StorageDbType db_type,
                               IncludeBinary include_binary) {
  void (*f)(const std::string&, T*) = AppendToContainer;
  DocDBDebugDump(rocksdb, db_type, include_binary, std::bind(f, _1, out));
}

template
void DocDBDebugDumpToContainer(
    rocksdb::DB* rocksdb, std::unordered_set<std::string>* out, StorageDbType db_type,
    IncludeBinary include_binary);

template
void DocDBDebugDumpToContainer(
    rocksdb::DB* rocksdb, std::vector<std::string>* out, StorageDbType db_type,
    IncludeBinary include_binary);

template <class T>
void DocDBDebugDumpToContainer(DocDB docdb, T* out, IncludeBinary include_binary) {
  DocDBDebugDumpToContainer(docdb.regular, out, StorageDbType::kRegular, include_binary);
  if (docdb.intents) {
    DocDBDebugDumpToContainer(docdb.intents, out, StorageDbType::kIntents, include_binary);
  }
}

template
void DocDBDebugDumpToContainer(
    DocDB docdb, std::unordered_set<std::string>* out, IncludeBinary include_binary);

template
void DocDBDebugDumpToContainer(
    DocDB docdb, std::vector<std::string>* out, IncludeBinary include_binary);

void DumpRocksDBToLog(rocksdb::DB* rocksdb, StorageDbType db_type) {
  std::vector<std::string> lines;
  DocDBDebugDumpToContainer(rocksdb, &lines, db_type);
  LOG(INFO) << AsString(db_type) << " DB dump:";
  for (const auto& line : lines) {
    LOG(INFO) << "  " << line;
  }
}

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, KeyBytes* out) {
  out->AppendValueType(ValueType::kTransactionId);
  out->AppendRawBytes(transaction_id.AsSlice());
}

DocHybridTimeBuffer::DocHybridTimeBuffer() {
  buffer_[0] = ValueTypeAsChar::kHybridTime;
}

Slice DocHybridTimeBuffer::EncodeWithValueType(const DocHybridTime& doc_ht) {
  auto end = doc_ht.EncodedInDocDbFormat(buffer_.data() + 1);
  return Slice(buffer_.data(), end);
}

CHECKED_STATUS IntentToWriteRequest(
    const Slice& transaction_id_slice,
    HybridTime commit_ht,
    const Slice& reverse_index_key,
    const Slice& reverse_index_value,
    rocksdb::Iterator* intent_iter,
    rocksdb::WriteBatch* regular_batch,
    IntraTxnWriteId* write_id) {
  DocHybridTimeBuffer doc_ht_buffer;
  intent_iter->Seek(reverse_index_value);
  if (!intent_iter->Valid() || intent_iter->key() != reverse_index_value) {
    LOG(DFATAL) << "Unable to find intent: " << reverse_index_value.ToDebugHexString()
                << " for " << reverse_index_key.ToDebugHexString();
    return Status::OK();
  }
  auto intent = VERIFY_RESULT(ParseIntentKey(intent_iter->key(), transaction_id_slice));

  if (intent.types.Test(IntentType::kStrongWrite)) {
    IntraTxnWriteId stored_write_id;
    Slice intent_value;
    RETURN_NOT_OK(DecodeIntentValue(
        intent_iter->value(), transaction_id_slice, &stored_write_id, &intent_value));

    // Write id should match to one that were calculated during append of intents.
    // Doing it just for sanity check.
    DCHECK_GE(stored_write_id, *write_id)
      << "Value: " << intent_iter->value().ToDebugHexString();
    *write_id = stored_write_id;

    // Intents for row locks should be ignored (i.e. should not be written as regular records).
    if (intent_value.starts_with(ValueTypeAsChar::kRowLock)) {
      return Status::OK();
    }

    // After strip of prefix and suffix intent_key contains just SubDocKey w/o a hybrid time.
    // Time will be added when writing batch to RocksDB.
    std::array<Slice, 2> key_parts = {{
        intent.doc_path,
        doc_ht_buffer.EncodeWithValueType(commit_ht, *write_id),
    }};
    std::array<Slice, 2> value_parts = {{
        intent.doc_ht,
        intent_value,
    }};

    // Useful when debugging transaction failure.
#if defined(DUMP_APPLY)
    SubDocKey sub_doc_key;
    CHECK_OK(sub_doc_key.FullyDecodeFrom(intent.doc_path, HybridTimeRequired::kFalse));
    if (!sub_doc_key.subkeys().empty() && sub_doc_key.subkeys().front().GetColumnId() == 11) {
      CHECK_OK(value.DecodeFromValue(intent_value));
      auto txn_id = FullyDecodeTransactionId(transaction_id_slice);
      LOG(INFO) << "Apply: " << sub_doc_key.doc_key().hashed_group().front()
                << ", time: " << commit_ht << ", txn: " << txn_id
                << ", raw: " << intent_iter->key().ToDebugHexString()
                << ", value: " << value.GetString()
                << ", subkey: " << sub_doc_key.subkeys().front();
      LOG(INFO) << txn_id << " APPLY: " << sub_doc_key.doc_key().hashed_group().front().GetInt32()
                << " = " << value.GetString();
    }
#endif

    regular_batch->Put(key_parts, value_parts);
    ++*write_id;
  }

  return Status::OK();
}

Status PrepareApplyIntentsBatch(
    const TransactionId& transaction_id, HybridTime commit_ht, const KeyBounds* key_bounds,
    rocksdb::WriteBatch* regular_batch,
    rocksdb::DB* intents_db, rocksdb::WriteBatch* intents_batch) {
  // regular_batch or intents_batch could be null. In this case we don't fill apply batch for
  // appropriate DB.

  KeyBytes txn_reverse_index_prefix;
  Slice transaction_id_slice = transaction_id.AsSlice();
  AppendTransactionKeyPrefix(transaction_id, &txn_reverse_index_prefix);
  txn_reverse_index_prefix.AppendValueType(ValueType::kMaxByte);
  Slice key_prefix = txn_reverse_index_prefix.AsSlice();
  key_prefix.remove_suffix(1);
  Slice reverse_index_upperbound = txn_reverse_index_prefix.AsSlice();

  auto reverse_index_iter = CreateRocksDBIterator(
      intents_db, &KeyBounds::kNoBounds, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none,
      rocksdb::kDefaultQueryId, nullptr /* read_filter */, &reverse_index_upperbound);

  BoundedRocksDbIterator intent_iter;
  // If we don't have regular_batch, it means that we just removing intents.
  // We don't need intent iterator, since reverse index iterator is enough in this case.
  if (regular_batch) {
    intent_iter = CreateRocksDBIterator(
        intents_db, key_bounds, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none,
        rocksdb::kDefaultQueryId);
  }

  reverse_index_iter.Seek(key_prefix);

  DocHybridTimeBuffer doc_ht_buffer;

  const auto& log_prefix = intents_db->GetOptions().log_prefix;

  IntraTxnWriteId write_id = 0;
  while (reverse_index_iter.Valid()) {
    rocksdb::Slice key_slice(reverse_index_iter.key());

    if (!key_slice.starts_with(key_prefix)) {
      break;
    }

    VLOG(4) << log_prefix << "Apply reverse index record to ["
            << (regular_batch ? "R" : "") << (intents_batch ? "I" : "")
            << "]: " << EntryToString(reverse_index_iter, StorageDbType::kIntents);

    // If the key ends at the transaction id then it is transaction metadata (status tablet,
    // isolation level etc.).
    if (key_slice.size() > txn_reverse_index_prefix.size()) {
      auto reverse_index_value = reverse_index_iter.value();
      if (!reverse_index_value.empty() && reverse_index_value[0] == ValueTypeAsChar::kBitSet) {
        CHECK(!FLAGS_TEST_fail_on_replicated_batch_idx_set_in_txn_record);
        reverse_index_value.remove_prefix(1);
        RETURN_NOT_OK(OneWayBitmap::Skip(&reverse_index_value));
      }

      // Value of reverse index is a key of original intent record, so seek it and check match.
      if (regular_batch &&
          (!key_bounds || key_bounds->IsWithinBounds(reverse_index_iter.value()))) {
        RETURN_NOT_OK(IntentToWriteRequest(
            transaction_id_slice, commit_ht, reverse_index_iter.key(), reverse_index_value,
            &intent_iter, regular_batch, &write_id));
      }

      if (intents_batch) {
        intents_batch->SingleDelete(reverse_index_value);
      }
    }

    if (intents_batch) {
      intents_batch->SingleDelete(reverse_index_iter.key());
    }

    reverse_index_iter.Next();
  }

  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
