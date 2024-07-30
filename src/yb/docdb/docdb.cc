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
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/intent.h"
#include "yb/dockv/subdocument.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet_metrics.h"

#include "yb/util/bitmap.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"
#include "yb/util/fast_varint.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/util/errcodes.h"

using std::string;
using std::stringstream;
using std::unique_ptr;
using std::vector;

using namespace std::placeholders;

DEFINE_UNKNOWN_int32(cdc_max_stream_intent_records, 1680,
             "Max number of intent records allowed in single cdc batch. ");

namespace yb {
namespace docdb {

using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryTypeAsChar;

namespace {

// key should be valid prefix of doc key, ending with some complete primitive value or group end.
Status ApplyIntent(
    RefCntPrefix key, dockv::IntentTypeSet intent_types,
    LockBatchEntries<RefCntPrefix>* keys_locked) {
  RSTATUS_DCHECK(!intent_types.None(), InternalError, "Empty intent types is not allowed");
  // Have to strip kGroupEnd from end of key, because when only hash key is specified, we will
  // get two kGroupEnd at end of strong intent.
  RETURN_NOT_OK(dockv::RemoveGroupEndSuffix(&key));
  keys_locked->push_back(
      LockBatchEntry<RefCntPrefix> {std::move(key), intent_types});
  return Status::OK();
}

Status FormSharedLock(
    ObjectLockPrefix key, dockv::IntentTypeSet intent_types,
    LockBatchEntries<ObjectLockPrefix>* keys_locked) {
  SCHECK(!intent_types.None(), InternalError, "Empty intent types is not allowed");
  keys_locked->push_back(
      LockBatchEntry<ObjectLockPrefix> {.key = std::move(key), .intent_types = intent_types});
  return Status::OK();
}

Result<DetermineKeysToLockResult<RefCntPrefix>> DetermineKeysToLock(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const ArenaList<LWKeyValuePairPB>& read_pairs,
    IsolationLevel isolation_level,
    RowMarkType row_mark_type,
    bool transactional_table,
    dockv::PartialRangeKeyIntents partial_range_key_intents) {
  DetermineKeysToLockResult<RefCntPrefix> result;
  boost::container::small_vector<RefCntPrefix, 8> doc_paths;
  boost::container::small_vector<size_t, 32> key_prefix_lengths;
  result.need_read_snapshot = false;
  for (const auto& doc_op : doc_write_ops) {
    doc_paths.clear();
    IsolationLevel level;
    RETURN_NOT_OK(doc_op->GetDocPaths(GetDocPathsMode::kLock, &doc_paths, &level));
    if (isolation_level != IsolationLevel::NON_TRANSACTIONAL) {
      level = isolation_level;
    }
    const auto require_read_snapshot = doc_op->RequireReadSnapshot();
    result.need_read_snapshot |= require_read_snapshot;
    auto intent_types = dockv::GetIntentTypesForWrite(level);
    if (isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION &&
        require_read_snapshot) {
      intent_types = dockv::IntentTypeSet(
          {dockv::IntentType::kStrongRead, dockv::IntentType::kStrongWrite});
    }
    // TODO(#20662): Assert for the following invariant: the set of (key, intent type) for which
    // in-memory locks are acquired should be a superset of all (key, intent type) pairs which are
    // either: (a) used to check conflicts against or (b) written to intents db. This is to ensure
    // that no new conflicting (key, intent type) is added until the operation is done checking for
    // conflicts and has successfully written the data in intents/ regular db.
    for (const auto& doc_path : doc_paths) {
      key_prefix_lengths.clear();
      auto doc_path_slice = doc_path.as_slice();
      RETURN_NOT_OK(dockv::SubDocKey::DecodePrefixLengths(doc_path_slice, &key_prefix_lengths));
      // At least entire doc_path should be returned, so empty key_prefix_lengths is an error.
      if (key_prefix_lengths.empty()) {
        return STATUS_FORMAT(Corruption, "Unable to decode key prefixes from: $0",
                             doc_path_slice.ToDebugHexString());
      }
      // We will acquire strong lock on the full doc_path, so remove it from list of weak locks.
      key_prefix_lengths.pop_back();
      auto partial_key = doc_path;
      // Acquire weak lock on empty key for transactional tables, unless specified key is already
      // empty.
      // For doc paths having cotable id/colocation id, a weak lock on the colocated table would be
      // acquired as part of acquiring weak locks on the prefixes. We should not acquire weak lock
      // on the empty key since it would lead to a weak lock on the parent table of the host tablet.
      auto has_cotable_id = doc_path_slice.starts_with(KeyEntryTypeAsChar::kTableId);
      auto has_colocation_id = doc_path_slice.starts_with(KeyEntryTypeAsChar::kColocationId);
      if (doc_path.size() > 0 && transactional_table && !(has_cotable_id || has_colocation_id)) {
        partial_key.Resize(0);
        RETURN_NOT_OK(ApplyIntent(
            partial_key, MakeWeak(intent_types), &result.lock_batch));
      }
      for (auto prefix_length : key_prefix_lengths) {
        partial_key.Resize(prefix_length);
        RETURN_NOT_OK(ApplyIntent(
            partial_key, MakeWeak(intent_types), &result.lock_batch));
      }

      RETURN_NOT_OK(ApplyIntent(doc_path, intent_types, &result.lock_batch));
    }
  }

  if (!read_pairs.empty()) {
    RETURN_NOT_OK(EnumerateIntents(
        read_pairs,
        [&result, intent_types = dockv::GetIntentTypesForRead(isolation_level, row_mark_type)](
            auto ancestor_doc_key, auto, auto, auto* key, auto, auto is_row_lock) {
          auto actual_intents = GetIntentTypes(intent_types, is_row_lock);
          return ApplyIntent(
              RefCntPrefix(key->AsSlice()),
              ancestor_doc_key ? MakeWeak(actual_intents) : actual_intents,
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
template <typename T>
void FilterKeysToLock(LockBatchEntries<T> *keys_locked) {
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

} // namespace

Result<PrepareDocWriteOperationResult> PrepareDocWriteOperation(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const ArenaList<LWKeyValuePairPB>& read_pairs,
    tablet::TabletMetrics* tablet_metrics,
    IsolationLevel isolation_level,
    RowMarkType row_mark_type,
    bool transactional_table,
    bool write_transaction_metadata,
    CoarseTimePoint deadline,
    dockv::PartialRangeKeyIntents partial_range_key_intents,
    SharedLockManager *lock_manager) {
  PrepareDocWriteOperationResult result;

  auto determine_keys_to_lock_result = VERIFY_RESULT(DetermineKeysToLock(
      doc_write_ops, read_pairs, isolation_level, row_mark_type,
      transactional_table, partial_range_key_intents));
  VLOG_WITH_FUNC(4) << "determine_keys_to_lock_result=" << determine_keys_to_lock_result.ToString();
  if (determine_keys_to_lock_result.lock_batch.empty() && !write_transaction_metadata) {
    LOG(ERROR) << "Empty lock batch, doc_write_ops: " << yb::ToString(doc_write_ops)
               << ", read pairs: " << AsString(read_pairs);
    return STATUS(Corruption, "Empty lock batch");
  }
  result.need_read_snapshot = determine_keys_to_lock_result.need_read_snapshot;

  FilterKeysToLock<RefCntPrefix>(&determine_keys_to_lock_result.lock_batch);
  VLOG_WITH_FUNC(4) << "filtered determine_keys_to_lock_result="
                    << determine_keys_to_lock_result.ToString();
  const MonoTime start_time = (tablet_metrics != nullptr) ? MonoTime::Now() : MonoTime();
  result.lock_batch = LockBatch(
      lock_manager, std::move(determine_keys_to_lock_result.lock_batch), deadline);
  auto lock_status = result.lock_batch.status();
  if (!lock_status.ok()) {
    if (tablet_metrics != nullptr) {
      tablet_metrics->Increment(tablet::TabletCounters::kFailedBatchLock);
    }
    return lock_status.CloneAndAppend(
        Format("Timeout: $0", deadline - ToCoarse(start_time)));
  }
  if (tablet_metrics != nullptr) {
    const MonoDelta elapsed_time = MonoTime::Now().GetDeltaSince(start_time);
    tablet_metrics->Increment(
        tablet::TabletEventStats::kWriteLockLatency,
        make_unsigned(elapsed_time.ToMicroseconds()));
  }

  return result;
}

Status AssembleDocWriteBatch(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                             const ReadOperationData& read_operation_data,
                             const DocDB& doc_db,
                             SchemaPackingProvider* schema_packing_provider /*null okay*/,
                             std::reference_wrapper<const ScopedRWOperation> pending_op,
                             LWKeyValueWriteBatchPB* write_batch,
                             InitMarkerBehavior init_marker_behavior,
                             std::atomic<int64_t>* monotonic_counter,
                             HybridTime* restart_read_ht,
                             const string& table_name) {
  DCHECK_ONLY_NOTNULL(restart_read_ht);
  DocWriteBatch doc_write_batch(doc_db, init_marker_behavior, pending_op, monotonic_counter);

  DocOperationApplyData data = {
    .doc_write_batch = &doc_write_batch,
    .read_operation_data = read_operation_data,
    .restart_read_ht = restart_read_ht,
    .schema_packing_provider = schema_packing_provider,
  };

  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    Status s = doc_op->Apply(data);
    if (s.IsQLError() && doc_op->OpType() == DocOperation::Type::QL_WRITE_OPERATION) {
      std::string error_msg;
      if (ql::GetErrorCode(s) == ql::ErrorCode::CONDITION_NOT_SATISFIED) {
        // Generating the error message here because 'table_name'
        // is not available on the lower level - in doc_op->Apply().
        error_msg = Format("Condition on table $0 was not satisfied.", table_name);
      } else {
        error_msg = s.message().ToBuffer();
      }
      // Ensure we set appropriate error in the response object for QL errors.
      const auto& resp = down_cast<QLWriteOperation*>(doc_op.get())->response();
      resp->set_status(QLResponsePB::YQL_STATUS_QUERY_ERROR);
      resp->set_error_message(std::move(error_msg));
      continue;
    }

    RETURN_NOT_OK(s);
  }
  doc_write_batch.MoveToWriteBatchPB(write_batch);
  return Status::OK();
}

Status EnumerateIntents(
    const ArenaList<LWKeyValuePairPB>& kv_pairs,
    const dockv::EnumerateIntentsCallback& functor,
    dockv::PartialRangeKeyIntents partial_range_key_intents) {
  if (kv_pairs.empty()) {
    return Status::OK();
  }
  KeyBytes encoded_key;

  auto it = kv_pairs.begin();
  for (;;) {
    const auto& kv_pair = *it;
    dockv::LastKey last_key(++it == kv_pairs.end());
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());
    RETURN_NOT_OK(dockv::EnumerateIntents(
        kv_pair.key(), kv_pair.value(), functor, &encoded_key, partial_range_key_intents,
        last_key));
    if (last_key) {
      break;
    }
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
  const uint64_t& max_records = FLAGS_cdc_max_stream_intent_records;
  uint64_t cur_records = 0;

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
        if (cur_records >= max_records) {
          return ApplyTransactionState{
              .key = key_slice.ToBuffer(), .write_id = write_id, .aborted = {}};
        }
        {
          intent_iter.Seek(reverse_index_value);
          if (!VERIFY_RESULT(intent_iter.CheckedValid()) ||
              intent_iter.key() != reverse_index_value) {
            LOG(WARNING) << "Unable to find intent: " << reverse_index_value.ToDebugHexString()
                         << " for " << key_slice.ToDebugHexString()
                         << ", transactionId: " << transaction_id;
            return ApplyTransactionState{};
          }

          auto intent = VERIFY_RESULT(ParseIntentKey(intent_iter.key(), transaction_id_slice));

          if (intent.types.Test(dockv::IntentType::kStrongWrite)) {
            auto decoded_value = VERIFY_RESULT(dockv::DecodeIntentValue(
                intent_iter.value(), &transaction_id_slice));
            write_id = decoded_value.write_id;

            if (decoded_value.body.starts_with(dockv::ValueEntryTypeAsChar::kRowLock)) {
              reverse_index_iter.Next();
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
            Slice(key_parts, &(intent_metadata.key_buf));
            intent_metadata.key = intent.doc_path;
            Slice(value_parts, &(intent_metadata.value_buf));
            intent_metadata.value = decoded_value.body;
            intent_metadata.reverse_index_key = key_slice.ToBuffer();
            intent_metadata.write_id = write_id;
            intent_metadata.intent_ht = doc_ht;
            intent_metadata.ht = Slice(ht_parts, &intent_metadata.ht_buf);

            (*key_value_intents).push_back(intent_metadata);

            VLOG(4) << "The size of intentKeyValues in GetIntentList "
                    << (*key_value_intents).size();
            ++cur_records;
            ++write_id;
          }
        }
      }
    }
    reverse_index_iter.Next();
  }
  RETURN_NOT_OK(reverse_index_iter.status());

  return ApplyTransactionState{};
}

std::string ApplyTransactionState::ToString() const {
  return Format(
      "{ key: $0 write_id: $1 aborted: $2 }", Slice(key).ToDebugString(), write_id, aborted);
}

void CombineExternalIntents(
    const TransactionId& txn_id,
    SubTransactionId subtransaction_id,
    ExternalIntentsProvider* provider) {
  // External intents are stored in the following format:
  //   key:   kExternalTransactionId, txn_id
  //   value: kUuid involved_tablet [kSubTransactionId subtransaction_ID]
  //          kExternalIntents size(intent1_key), intent1_key, size(intent1_value), intent1_value,
  //          size(intent2_key)...  0
  // where size is encoded as varint.

  dockv::KeyBytes buffer;
  buffer.AppendKeyEntryType(KeyEntryType::kExternalTransactionId);
  buffer.AppendRawBytes(txn_id.AsSlice());
  provider->SetKey(buffer.AsSlice());
  buffer.Clear();
  buffer.AppendKeyEntryType(KeyEntryType::kUuid);
  buffer.AppendRawBytes(provider->InvolvedTablet().AsSlice());
  if (subtransaction_id != kMinSubTransactionId) {
    buffer.AppendKeyEntryType(KeyEntryType::kSubTransactionId);
    buffer.AppendUInt32(subtransaction_id);
  }
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

// We associate a list of <KeyEntryType, IntentTypeSet> to each table lock type such that the
// table lock conflict matrix of postgres is preserved.
//
// For instance, let's consider 'ROW_SHARE' and 'EXCLUSIVE' lock modes.
// 1. 'ROW_SHARE' lock mode on object would lead to the following keys
//    [<object/object hash/other prefix> kWeakObjectLock]   [kStrongRead]
// 2. 'EXCLUSIVE' lock mode on the same object would lead to the following keys
//    [<object/object hash/other prefix> kWeakObjectLock]   [kWeakWrite]
//    [<object/object hash/other prefix> kStrongObjectLock] [kStrongRead, kStrongWrite]
//
// When checking conflicts for the same key, '[<object/object hash/other prefix> kWeakObjectLock]'
// in this case, we see that the intents requested are [kStrongRead] and [kWeakWrite] for modes
// 'ROW_SHARE' and 'EXCLUSIVE' respectively. And since the above intenttype sets conflict among
// themselves, we successfully detect the conflict.
const std::vector<std::pair<KeyEntryType, dockv::IntentTypeSet>>& GetEntriesForLockType(
    TableLockType lock) {
  static const std::array<
      std::vector<std::pair<KeyEntryType, dockv::IntentTypeSet>>,
      TableLockType_ARRAYSIZE> lock_entries = {{
    // NONE
    {{}},
    // ACCESS_SHARE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kWeakRead}}
    }},
    // ROW_SHARE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kStrongRead}}
    }},
    // ROW_EXCLUSIVE
    {{
      {KeyEntryType::kStrongObjectLock, dockv::IntentTypeSet {dockv::IntentType::kWeakRead}}
    }},
    // SHARE_UPDATE_EXCLUSIVE
    {{
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kStrongRead, dockv::IntentType::kWeakWrite}
      }
    }},
    // SHARE
    {{
      {KeyEntryType::kStrongObjectLock, dockv::IntentTypeSet {dockv::IntentType::kStrongWrite}}
    }},
    // SHARE_ROW_EXCLUSIVE
    {{
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kWeakRead, dockv::IntentType::kStrongWrite}
      }
    }},
    // EXCLUSIVE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kWeakWrite}},
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kStrongRead, dockv::IntentType::kStrongWrite}
      }
    }},
    // ACCESS_EXCLUSIVE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kStrongWrite}},
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kStrongRead, dockv::IntentType::kStrongWrite}
      }
    }}
  }};
  return lock_entries[lock];
}

// Returns a DetermineKeysToLockResult object with its lock_batch containing a list of entries with
// 'key' as <object id, KeyEntry> and 'intent_types' set.
Result<DetermineKeysToLockResult<ObjectLockPrefix>> DetermineObjectsToLock(
    const google::protobuf::RepeatedPtrField<ObjectLockPB>& objects_to_lock) {
  DetermineKeysToLockResult<ObjectLockPrefix> result;
  for (const auto& object_lock : objects_to_lock) {
    SCHECK(object_lock.has_id(), IllegalState, "Expected non-empty id in ObjectLockPB");
    for (const auto& [lock_key, intent_types] : GetEntriesForLockType(object_lock.lock_type())) {
      ObjectLockPrefix key(object_lock.id(), lock_key);
      RETURN_NOT_OK(FormSharedLock(key, intent_types, &result.lock_batch));
    }
  }
  FilterKeysToLock<ObjectLockPrefix>(&result.lock_batch);
  return result;
}

}  // namespace docdb
}  // namespace yb
