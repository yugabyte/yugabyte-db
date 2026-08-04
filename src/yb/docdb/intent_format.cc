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

#include "yb/docdb/intent_format.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/iter_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/options.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/memory/arena_list.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

using namespace std::literals;

namespace yb::docdb {

using dockv::KeyBytes;
using dockv::KeyEntryTypeAsChar;
using dockv::SubDocKey;

std::string DecodeStrongWriteIntentResult::ToString() const {
  return Format(
      "{ intent_prefix: $0 intent_value: $1 intent_time: $2 value_time: $3 "
      "same_transaction: $4 intent_types: $5 }",
      intent_prefix.ToDebugHexString(), intent_value.ToDebugHexString(), intent_time, value_time,
      same_transaction, intent_types);
}

const EncodedDocHybridTime& DecodeStrongWriteIntentResult::MaxAllowedValueTime(
    const EncodedReadHybridTime& read_time) const {
  if (same_transaction) {
    return read_time.in_txn_limit;
  }
  return intent_time > read_time.local_limit ? read_time.read : read_time.global_limit;
}

// Decodes intent based on intent_iterator and its transaction commit time if intent is a strong
// write intent, intent is not for row locking, and transaction is already committed at specified
// time or is current transaction.
// Returns HybridTime::kMin as value_time otherwise.
// For current transaction returns intent record hybrid time as value_time.
// Consumes intent from value_slice leaving only value itself.
Result<DecodeStrongWriteIntentResult> DecodeStrongWriteIntent(
    const TransactionOperationContext& txn_op_context,
    rocksdb::Iterator* intent_iter,
    TransactionStatusCache* transaction_status_cache) {
  DecodeStrongWriteIntentResult result;
  auto decoded_intent_key = VERIFY_RESULT(dockv::DecodeIntentKey(intent_iter->key()));
  result.intent_prefix = decoded_intent_key.intent_prefix;
  result.intent_types = decoded_intent_key.intent_types;
  if (result.intent_types.Test(dockv::IntentType::kStrongWrite)) {
    auto intent_value = intent_iter->value();
    auto decoded_intent_value = VERIFY_RESULT(dockv::DecodeIntentValue(intent_value));

    const auto& decoded_txn_id = decoded_intent_value.transaction_id;
    auto decoded_subtxn_id = decoded_intent_value.subtransaction_id;

    result.intent_value = decoded_intent_value.body;
    result.intent_time = decoded_intent_key.doc_ht;
    result.same_transaction = decoded_txn_id == txn_op_context.transaction_id;

    // By setting the value time to kMin, we ensure the caller ignores this intent. This is true
    // because the caller is skipping all intents written before or at the same time as
    // intent_dht_from_same_txn_ or resolved_intent_txn_dht_, which of course are greater than or
    // equal to DocHybridTime::kMin.
    if (result.intent_value.starts_with(dockv::ValueEntryTypeAsChar::kRowLock)) {
      result.value_time.Assign(EncodedDocHybridTime::kMin);
    } else if (result.same_transaction) {
      const auto aborted = txn_op_context.subtransaction.aborted.Test(decoded_subtxn_id);
      if (!aborted) {
        result.value_time = decoded_intent_key.doc_ht;
      } else {
        // If this intent is from the same transaction, we can check the aborted set from this
        // txn_op_context to see whether the intent is still live. If not, mask it from the caller.
        result.value_time.Assign(EncodedDocHybridTime::kMin);
      }
      VLOG(4) << "Same transaction: " << decoded_txn_id << ", aborted: " << aborted
              << ", original doc_ht: " << decoded_intent_key.doc_ht.ToString();
    } else {
      auto commit_data =
          VERIFY_RESULT(transaction_status_cache->GetTransactionLocalState(decoded_txn_id));
      auto commit_ht = commit_data.commit_ht;
      const auto& aborted_subtxn_set = commit_data.aborted_subtxn_set;
      auto is_aborted_subtxn = aborted_subtxn_set.Test(decoded_subtxn_id);
      result.value_time.Assign(
          commit_ht == HybridTime::kMin || is_aborted_subtxn
              ? DocHybridTime::kMin
              : DocHybridTime(commit_ht, decoded_intent_value.write_id));
      VLOG(4) << "Transaction id: " << decoded_txn_id
              << ", subtransaction id: " << decoded_subtxn_id
              << ", same transaction: " << result.same_transaction
              << ", value time: " << result.value_time
              << ", commit ht: " << commit_ht
              << ", value: " << result.intent_value.ToDebugHexString()
              << ", aborted subtxn set: " << aborted_subtxn_set.ToString();
    }
  } else {
    result.value_time.Assign(EncodedDocHybridTime::kMin);
  }
  return result;
}

Status EnumerateIntents(
    const ArenaList<LWKeyValuePairPB>& kv_pairs,
    const dockv::EnumerateIntentsCallback& functor,
    dockv::PartialRangeKeyIntents partial_range_key_intents,
    dockv::SkipPrefixLocks skip_prefix_locks) {
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
    boost::tribool pk_is_known = kv_pair.has_pk_is_known() ?
        boost::tribool(kv_pair.pk_is_known()) : boost::indeterminate;
    dockv::EnumerateIntentsCallback new_functor = [&functor, &pk_is_known] (
        auto ancestor_doc_key, auto full_doc_key, auto value, auto* key, auto last_key,
        auto is_row_lock, auto is_top_level_key, auto) {
      return functor(ancestor_doc_key, full_doc_key, value, key, last_key, is_row_lock,
                     is_top_level_key, pk_is_known);
    };
    RETURN_NOT_OK(dockv::EnumerateIntents(
        kv_pair.key(), kv_pair.value(), new_functor, &encoded_key, partial_range_key_intents,
        last_key, skip_prefix_locks ? dockv::SkipPrefix::kTrue : dockv::SkipPrefix::kFalse));
    if (last_key) {
      break;
    }
  }

  return Status::OK();
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

}  // namespace yb::docdb
