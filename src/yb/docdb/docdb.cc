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

#include "yb/docdb/docdb.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/rocksdb_writer.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/bitmap.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"
#include "yb/util/fast_varint.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

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

DEFINE_int32(cdc_max_stream_intent_records, 1000,
             "Max number of intent records allowed in single cdc batch. ");

namespace yb {
namespace docdb {

namespace {

// key should be valid prefix of doc key, ending with some complete pritimive value or group end.
Status ApplyIntent(RefCntPrefix key,
                           const IntentTypeSet intent_types,
                           LockBatchEntries *keys_locked) {
  // Have to strip kGroupEnd from end of key, because when only hash key is specified, we will
  // get two kGroupEnd at end of strong intent.
  size_t size = key.size();
  if (size > 0) {
    if (key.data()[0] == KeyEntryTypeAsChar::kGroupEnd) {
      if (size != 1) {
        return STATUS_FORMAT(Corruption, "Key starting with group end: $0",
            key.as_slice().ToDebugHexString());
      }
      size = 0;
    } else {
      while (key.data()[size - 1] == KeyEntryTypeAsChar::kGroupEnd) {
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

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(lock_batch, need_read_snapshot);
  }
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
      // We will acquire strong lock on the full doc_path, so remove it from list of weak locks.
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
    const auto strong_read_intent_types = GetStrongIntentTypeSet(
        isolation_level, OperationKind::kRead, row_mark_type);
    RETURN_NOT_OK(EnumerateIntents(
        read_pairs,
        [&result, &strong_read_intent_types](
            IntentStrength strength, FullDocKey, Slice value, KeyBytes* key, LastKey) {
          RefCntPrefix prefix(key->AsSlice());
          return ApplyIntent(prefix,
                             strength == IntentStrength::kStrong
                                ? strong_read_intent_types
                                : StrongToWeak(strong_read_intent_types),
                             &result.lock_batch);
        }, partial_range_key_intents));
  }

  return result;
}

// Collapse keys_locked into a unique set of keys with intent_types representing the union of
// intent_types originally present. In other words, suppose keys_locked is originally the following:
// [
//   (k1, {kWeakRead, kWeakWrite}),
//   (k1, {kStrongRead}),
//   (k2, {kWeakRead}),
//   (k3, {kStrongRead}),
//   (k2, {kStrongWrite}),
// ]
// Then after calling FilterKeysToLock we will have:
// [
//   (k1, {kWeakRead, kWeakWrite, kStrongRead}),
//   (k2, {kWeakRead}),
//   (k3, {kStrongRead, kStrongWrite}),
// ]
// Note that only keys which appear in order in keys_locked will be collapsed in this manner.
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

Result<PrepareDocWriteOperationResult> PrepareDocWriteOperation(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const google::protobuf::RepeatedPtrField<KeyValuePairPB>& read_pairs,
    const scoped_refptr<Histogram>& write_lock_latency,
    const IsolationLevel isolation_level,
    const OperationKind operation_kind,
    const RowMarkType row_mark_type,
    bool transactional_table,
    bool write_transaction_metadata,
    CoarseTimePoint deadline,
    PartialRangeKeyIntents partial_range_key_intents,
    SharedLockManager *lock_manager) {
  PrepareDocWriteOperationResult result;

  auto determine_keys_to_lock_result = VERIFY_RESULT(DetermineKeysToLock(
      doc_write_ops, read_pairs, isolation_level, operation_kind, row_mark_type,
      transactional_table, partial_range_key_intents));
  VLOG_WITH_FUNC(4) << "determine_keys_to_lock_result=" << determine_keys_to_lock_result.ToString();
  if (determine_keys_to_lock_result.lock_batch.empty() && !write_transaction_metadata) {
    LOG(ERROR) << "Empty lock batch, doc_write_ops: " << yb::ToString(doc_write_ops)
               << ", read pairs: " << yb::ToString(read_pairs);
    return STATUS(Corruption, "Empty lock batch");
  }
  result.need_read_snapshot = determine_keys_to_lock_result.need_read_snapshot;

  FilterKeysToLock(&determine_keys_to_lock_result.lock_batch);
  VLOG_WITH_FUNC(4) << "filtered determine_keys_to_lock_result="
                    << determine_keys_to_lock_result.ToString();
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

Status AssembleDocWriteBatch(const vector<unique_ptr<DocOperation>>& doc_write_ops,
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

namespace {

Status NotEnoughBytes(size_t present, size_t required, const Slice& full) {
  return STATUS_FORMAT(
      Corruption, "Not enough bytes in external intents $0 while $1 expected, full: $2",
      present, required, full.ToDebugHexString());
}

Status PrepareApplyExternalIntentsBatch(
    HybridTime commit_ht,
    const Slice& original_input_value,
    rocksdb::WriteBatch* regular_batch,
    IntraTxnWriteId* write_id) {
  auto input_value = original_input_value;
  DocHybridTimeBuffer doc_ht_buffer;
  RETURN_NOT_OK(input_value.consume_byte(KeyEntryTypeAsChar::kUuid));
  RETURN_NOT_OK(Uuid::FromSlice(input_value.Prefix(kUuidSize)));
  input_value.remove_prefix(kUuidSize);
  RETURN_NOT_OK(input_value.consume_byte(KeyEntryTypeAsChar::kExternalIntents));
  for (;;) {
    auto key_size = VERIFY_RESULT(util::FastDecodeUnsignedVarInt(&input_value));
    if (key_size == 0) {
      break;
    }
    if (input_value.size() < key_size) {
      return NotEnoughBytes(input_value.size(), key_size, original_input_value);
    }
    auto output_key = input_value.Prefix(key_size);
    input_value.remove_prefix(key_size);
    auto value_size = VERIFY_RESULT(util::FastDecodeUnsignedVarInt(&input_value));
    if (input_value.size() < value_size) {
      return NotEnoughBytes(input_value.size(), value_size, original_input_value);
    }
    auto output_value = input_value.Prefix(value_size);
    input_value.remove_prefix(value_size);
    std::array<Slice, 2> key_parts = {{
        output_key,
        doc_ht_buffer.EncodeWithValueType(commit_ht, *write_id),
    }};
    std::array<Slice, 1> value_parts = {{
        output_value,
    }};
    regular_batch->Put(key_parts, value_parts);
    ++*write_id;
  }

  return Status::OK();
}

// Reads all stored external intents for provided transactions and prepares batches that will apply
// them into regular db and remove from intents db.
Status PrepareApplyExternalIntents(
    ExternalTxnApplyState* apply_external_transactions,
    rocksdb::WriteBatch* regular_batch,
    rocksdb::DB* intents_db,
    rocksdb::WriteBatch* intents_batch) {
  if (apply_external_transactions->empty()) {
    return Status::OK();
  }

  KeyBytes key_prefix;
  KeyBytes key_upperbound;
  Slice key_upperbound_slice;

  auto iter = CreateRocksDBIterator(
      intents_db, &KeyBounds::kNoBounds, BloomFilterMode::DONT_USE_BLOOM_FILTER,
      /* user_key_for_filter= */ boost::none,
      rocksdb::kDefaultQueryId, /* read_filter= */ nullptr, &key_upperbound_slice);

  for (auto& apply : *apply_external_transactions) {
    key_prefix.Clear();
    key_prefix.AppendKeyEntryType(KeyEntryType::kExternalTransactionId);
    key_prefix.AppendRawBytes(apply.first.AsSlice());

    key_upperbound = key_prefix;
    key_upperbound.AppendKeyEntryType(KeyEntryType::kMaxByte);
    key_upperbound_slice = key_upperbound.AsSlice();

    IntraTxnWriteId& write_id = apply.second.write_id;

    iter.Seek(key_prefix);
    while (iter.Valid()) {
      const Slice input_key(iter.key());

      if (!input_key.starts_with(key_prefix.AsSlice())) {
        break;
      }

      if (regular_batch) {
        RETURN_NOT_OK(PrepareApplyExternalIntentsBatch(
            apply.second.commit_ht, iter.value(), regular_batch, &write_id));
      }
      if (intents_batch) {
        intents_batch->SingleDelete(input_key);
      }

      iter.Next();
    }
  }

  return Status::OK();
}

ExternalTxnApplyState ProcessApplyExternalTransactions(const KeyValueWriteBatchPB& put_batch) {
  ExternalTxnApplyState result;
  for (const auto& apply : put_batch.apply_external_transactions()) {
    auto txn_id = CHECK_RESULT(FullyDecodeTransactionId(apply.transaction_id()));
    auto commit_ht = HybridTime(apply.commit_hybrid_time());
    result.emplace(
        txn_id,
        ExternalTxnApplyStateData{
          .commit_ht = commit_ht
        });
  }

  return result;
}

} // namespace

IntraTxnWriteId ExternalTxnIntentsState::GetWriteIdAndIncrement(const TransactionId& txn_id) {
  std::lock_guard<decltype(mutex_)> lock(mutex_);
  return map_[txn_id]++;
}

void ExternalTxnIntentsState::EraseEntry(const TransactionId& txn_id) {
  std::lock_guard<decltype(mutex_)> lock(mutex_);
  map_.erase(txn_id);
}

bool AddExternalPairToWriteBatch(
    const KeyValuePairPB& kv_pair,
    HybridTime hybrid_time,
    ExternalTxnApplyState* apply_external_transactions,
    rocksdb::WriteBatch* regular_write_batch,
    rocksdb::WriteBatch* intents_write_batch,
    ExternalTxnIntentsState* external_txns_intents_state) {
  DocHybridTimeBuffer doc_ht_buffer;
  DocHybridTimeWordBuffer inverted_doc_ht_buffer;

  CHECK(!kv_pair.key().empty());
  CHECK(!kv_pair.value().empty());

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
  auto txn_id = CHECK_RESULT(DecodeTransactionId(&key));
  auto it = apply_external_transactions->find(txn_id);
  if (it != apply_external_transactions->end()) {
    // The same write operation could contain external intents and instruct us to apply them.
    CHECK_OK(PrepareApplyExternalIntentsBatch(
        it->second.commit_ht, key_value, regular_write_batch, &it->second.write_id));
    if (external_txns_intents_state) {
      external_txns_intents_state->EraseEntry(txn_id);
    }
    return false;
  }

  int write_id = 0;
  if (external_txns_intents_state) {
    write_id = external_txns_intents_state->GetWriteIdAndIncrement(txn_id);
  }

  hybrid_time = kv_pair.has_external_hybrid_time() ?
      HybridTime(kv_pair.external_hybrid_time()) : hybrid_time;
  std::array<Slice, 2> key_parts = {{
      Slice(kv_pair.key()),
      doc_ht_buffer.EncodeWithValueType(hybrid_time, write_id),
  }};
  key_parts[1] = InvertEncodedDocHT(key_parts[1], &inverted_doc_ht_buffer);
  constexpr size_t kNumValueParts = 1;
  intents_write_batch->Put(key_parts, { &key_value, kNumValueParts });

  return false;
}

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
bool PrepareExternalWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::DB* intents_db,
    rocksdb::WriteBatch* regular_write_batch,
    rocksdb::WriteBatch* intents_write_batch,
    ExternalTxnIntentsState* external_txns_intents_state) {
  CHECK(put_batch.read_pairs().empty());

  auto apply_external_transactions = ProcessApplyExternalTransactions(put_batch);

  CHECK_OK(PrepareApplyExternalIntents(
      &apply_external_transactions, regular_write_batch, intents_db, intents_write_batch));

  bool has_non_external_kvs = false;
  for (const auto& write_pair : put_batch.write_pairs()) {
    has_non_external_kvs = AddExternalPairToWriteBatch(
        write_pair, hybrid_time, &apply_external_transactions, regular_write_batch,
        intents_write_batch, external_txns_intents_state) || has_non_external_kvs;
  }
  return has_non_external_kvs;
}

namespace {

// Checks if the given slice points to the part of an encoded SubDocKey past all of the subkeys
// (and definitely past all the hash/range keys). The only remaining part could be a hybrid time.
inline bool IsEndOfSubKeys(const Slice& key) {
  return key[0] == KeyEntryTypeAsChar::kGroupEnd &&
         (key.size() == 1 || key[1] == KeyEntryTypeAsChar::kHybridTime);
}

// Enumerates weak intent keys generated by considering specified prefixes of the given key and
// invoking the provided callback with each combination considered, stored in encoded_key_buffer.
// On return, *encoded_key_buffer contains the corresponding strong intent, for which the callback
// has not yet been called. It is left to the caller to use the final state of encoded_key_buffer.
//
// The prefixes of the key considered are as follows:
// 1. Up to and including the whole hash key.
// 2. Up to and including the whole range key, or if partial_range_key_intents is
//    PartialRangeKeyIntents::kTrue, then enumerate the prefix up to the end of each component of
//    the range key separately.
// 3. Up to and including each subkey component, separately.
//
// In any case, we stop short of enumerating the last intent key generated based on the above, as
// this represents the strong intent key and will be stored in encoded_key_buffer at the end of this
// call.
//
// The beginning of each intent key will also include any cotable_id or colocation_id,
// if present.
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

  const bool has_cotable_id    = *key.cdata() == KeyEntryTypeAsChar::kTableId;
  const bool has_colocation_id = *key.cdata() == KeyEntryTypeAsChar::kColocationId;
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
      is_table_root_key = key[kUuidSize + 1] == KeyEntryTypeAsChar::kGroupEnd;
    } else if (has_colocation_id) {
      const auto kMinExpectedSize = sizeof(ColocationId) + 2;
      if (key.size() < kMinExpectedSize) {
        return STATUS_FORMAT(
            Corruption,
            "Expected an encoded SubDocKey starting with a colocation id to be"
            " at least $0 bytes long",
            kMinExpectedSize);
      }
      encoded_key_buffer->AppendRawBytes(key.cdata(), sizeof(ColocationId) + 1);
      is_table_root_key = key[sizeof(ColocationId) + 1] == KeyEntryTypeAsChar::kGroupEnd;
    } else {
      is_table_root_key = *key.cdata() == KeyEntryTypeAsChar::kGroupEnd;
    }

    encoded_key_buffer->AppendKeyEntryType(KeyEntryType::kGroupEnd);

    if (is_table_root_key) {
      // This must be a "table root" (or "tablet root") key (no hash components, no range
      // components, but the cotable might still be there). We are not really considering the case
      // of any subkeys under the empty key, so we can return here.
      return Status::OK();
    }
  }

  // For any non-empty key we already know that the empty key intent is weak.
  RETURN_NOT_OK(functor(
      IntentStrength::kWeak, FullDocKey::kFalse, kEmptyIntentValue, encoded_key_buffer,
      LastKey::kFalse));

  auto hashed_part_size = VERIFY_RESULT(DocKey::EncodedSize(key, DocKeyPart::kUpToHash));

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
    encoded_key_buffer->AppendKeyEntryType(KeyEntryType::kGroupEnd);
    if (IsEndOfSubKeys(key)) {
      // This means the key ends at the hash component -- no range keys and no subkeys.
      return Status::OK();
    }

    // Generate a weak intent that only includes the hash component.
    RETURN_NOT_OK(functor(
        IntentStrength::kWeak, FullDocKey(key[0] == KeyEntryTypeAsChar::kGroupEnd),
        kEmptyIntentValue, encoded_key_buffer, LastKey::kFalse));

    // Remove the kGroupEnd we added a bit earlier so we can append some range components.
    encoded_key_buffer->RemoveLastByte();
  } else {
    // No hash component.
    key.remove_prefix(hashed_part_size);
  }

  // Range components.
  auto range_key_start = key.cdata();
  while (VERIFY_RESULT(ConsumePrimitiveValueFromKey(&key))) {
    // Append the consumed primitive value to encoded_key_buffer.
    encoded_key_buffer->AppendRawBytes(range_key_start, key.cdata() - range_key_start);
    // We always need kGroupEnd at the end to make this a valid encoded DocKey.
    encoded_key_buffer->AppendKeyEntryType(KeyEntryType::kGroupEnd);
    if (key.empty()) {
      return STATUS(Corruption, "Range key part is not terminated with a kGroupEnd");
    }
    if (IsEndOfSubKeys(key)) {
      // This is the last range key and there are no subkeys.
      return Status::OK();
    }
    FullDocKey full_doc_key(key[0] == KeyEntryTypeAsChar::kGroupEnd);
    if (partial_range_key_intents || full_doc_key) {
      RETURN_NOT_OK(functor(
          IntentStrength::kWeak, full_doc_key, kEmptyIntentValue, encoded_key_buffer,
          LastKey::kFalse));
    }
    encoded_key_buffer->RemoveLastByte();
    range_key_start = key.cdata();
  }

  // We still need to append the kGroupEnd byte that closes the range portion to our buffer.
  // The corresponding kGroupEnd has already been consumed from the key slice by the last call to
  // ConsumePrimitiveValueFromKey, which returned false.
  encoded_key_buffer->AppendKeyEntryType(KeyEntryType::kGroupEnd);

  // Subkey components.
  auto subkey_start = key.cdata();
  while (VERIFY_RESULT(SubDocKey::DecodeSubkey(&key))) {
    // Append the consumed value to encoded_key_buffer.
    encoded_key_buffer->AppendRawBytes(subkey_start, key.cdata() - subkey_start);
    if (key.empty() || *key.cdata() == KeyEntryTypeAsChar::kHybridTime) {
      // This was the last subkey.
      return Status::OK();
    }
    RETURN_NOT_OK(functor(
        IntentStrength::kWeak, FullDocKey::kTrue, kEmptyIntentValue, encoded_key_buffer,
        LastKey::kFalse));
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
  return functor(
      IntentStrength::kStrong, FullDocKey::kTrue, intent_value, encoded_key_buffer, last_key);
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

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, KeyBytes* out) {
  out->AppendKeyEntryType(KeyEntryType::kTransactionId);
  out->AppendRawBytes(transaction_id.AsSlice());
}

Result<ApplyTransactionState> GetIntentsBatch(
    const TransactionId& transaction_id,
    const KeyBounds* key_bounds,
    const ApplyTransactionState* stream_state,
    rocksdb::DB* intents_db,
    std::vector<IntentKeyValueForCDC>* key_value_intents) {
  KeyBytes txn_reverse_index_prefix;
  Slice transaction_id_slice = transaction_id.AsSlice();
  AppendTransactionKeyPrefix(transaction_id, &txn_reverse_index_prefix);
  txn_reverse_index_prefix.AppendKeyEntryType(KeyEntryType::kMaxByte);
  Slice key_prefix = txn_reverse_index_prefix.AsSlice();
  key_prefix.remove_suffix(1);
  const Slice reverse_index_upperbound = txn_reverse_index_prefix.AsSlice();

  auto reverse_index_iter = CreateRocksDBIterator(
      intents_db, &KeyBounds::kNoBounds, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none,
      rocksdb::kDefaultQueryId, nullptr /* read_filter */, &reverse_index_upperbound);

  BoundedRocksDbIterator intent_iter = CreateRocksDBIterator(
      intents_db, key_bounds, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none,
      rocksdb::kDefaultQueryId);

  reverse_index_iter.Seek(key_prefix);

  DocHybridTimeBuffer doc_ht_buffer;
  IntraTxnWriteId write_id = 0;
  if (stream_state != nullptr && stream_state->active() && stream_state->write_id != 0) {
    reverse_index_iter.Seek(stream_state->key);
    write_id = stream_state->write_id;
    reverse_index_iter.Next();
  }
  const uint64_t max_records = FLAGS_cdc_max_stream_intent_records;
  const uint64_t write_id_limit = write_id + max_records;

  while (reverse_index_iter.Valid()) {
    const Slice key_slice(reverse_index_iter.key());

    if (!key_slice.starts_with(key_prefix)) {
      break;
    }
    // If the key ends at the transaction id then it is transaction metadata (status tablet,
    // isolation level etc.).
    if (key_slice.size() > txn_reverse_index_prefix.size()) {
      auto reverse_index_value = reverse_index_iter.value();
      if (!reverse_index_value.empty() && reverse_index_value[0] == KeyEntryTypeAsChar::kBitSet) {
        reverse_index_value.remove_prefix(1);
        RETURN_NOT_OK(OneWayBitmap::Skip(&reverse_index_value));
      }
      // Value of reverse index is a key of original intent record, so seek it and check match.
      if ((!key_bounds || key_bounds->IsWithinBounds(reverse_index_iter.value()))) {
        // return when we have reached the batch limit.
        if (write_id >= write_id_limit) {
          return ApplyTransactionState{
              .key = key_slice.ToBuffer(),
              .write_id = write_id,
              .aborted = {},
          };
        }
        {
          intent_iter.Seek(reverse_index_value);
          if (!intent_iter.Valid() || intent_iter.key() != reverse_index_value) {
            LOG(WARNING) << "Unable to find intent: " << reverse_index_value.ToDebugHexString()
                         << " for " << key_slice.ToDebugHexString()
                         << ", transactionId: " << transaction_id;
            return ApplyTransactionState{};
          }

          auto intent = VERIFY_RESULT(ParseIntentKey(intent_iter.key(), transaction_id_slice));

          if (intent.types.Test(IntentType::kStrongWrite)) {
            auto decoded_value =
                VERIFY_RESULT(DecodeIntentValue(intent_iter.value(), &transaction_id_slice));
            write_id = decoded_value.write_id;

            if (decoded_value.body.starts_with(KeyEntryTypeAsChar::kRowLock)) {
              continue;
            }

            std::array<Slice, 1> key_parts = {{
                intent.doc_path,
            }};
            std::array<Slice, 1> value_parts = {{
                  decoded_value.body,
            }};
            std::array<Slice, 1> ht_parts = {{
                intent.doc_ht,
            }};

            auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(intent.doc_ht));

            IntentKeyValueForCDC intent_metadata;
            intent_metadata.key = Slice(key_parts, &(intent_metadata.key_buf));
            intent_metadata.value = Slice(value_parts, &(intent_metadata.value_buf));
            intent_metadata.reverse_index_key = key_slice.ToBuffer();
            intent_metadata.write_id = write_id;
            intent_metadata.intent_ht = doc_ht;
            intent_metadata.ht = Slice(ht_parts, &intent_metadata.ht_buf);

            (*key_value_intents).push_back(intent_metadata);

            VLOG(4) << "The size of intentKeyValues in GetIntentList "
                    << (*key_value_intents).size();
            ++write_id;
          }
        }
      }
    }
    reverse_index_iter.Next();
  }

  return ApplyTransactionState{};
}

std::string ApplyTransactionState::ToString() const {
  return Format(
      "{ key: $0 write_id: $1 aborted: $2 }", Slice(key).ToDebugString(), write_id, aborted);
}

void CombineExternalIntents(
    const TransactionId& txn_id,
    ExternalIntentsProvider* provider) {
  // External intents are stored in the following format:
  // key: kExternalTransactionId, txn_id
  // value: size(intent1_key), intent1_key, size(intent1_value), intent1_value, size(intent2_key)...
  // where size is encoded as varint.

  docdb::KeyBytes buffer;
  buffer.AppendKeyEntryType(KeyEntryType::kExternalTransactionId);
  buffer.AppendRawBytes(txn_id.AsSlice());
  provider->SetKey(buffer.AsSlice());
  buffer.Clear();
  buffer.AppendKeyEntryType(KeyEntryType::kUuid);
  buffer.AppendRawBytes(provider->InvolvedTablet().AsSlice());
  buffer.AppendKeyEntryType(KeyEntryType::kExternalIntents);
  while (auto key_value = provider->Next()) {
    buffer.AppendUInt64AsVarInt(key_value->first.size());
    buffer.AppendRawBytes(key_value->first);
    buffer.AppendUInt64AsVarInt(key_value->second.size());
    buffer.AppendRawBytes(key_value->second);
  }
  buffer.AppendUInt64AsVarInt(0);
  provider->SetValue(buffer.AsSlice());
}

}  // namespace docdb
}  // namespace yb
