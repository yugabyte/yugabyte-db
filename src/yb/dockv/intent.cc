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

#include "yb/dockv/intent.h"

#include "yb/util/logging.h"

#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/endian.h"

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status_format.h"

namespace yb::dockv {
namespace {

inline IntentTypeSet GetIntentTypes(IsolationLevel level, bool is_for_write = false) {
  switch (level) {
    case IsolationLevel::READ_COMMITTED:
    case IsolationLevel::SNAPSHOT_ISOLATION:
      return is_for_write
          ? IntentTypeSet({IntentType::kStrongRead, IntentType::kStrongWrite}) : IntentTypeSet();
    case IsolationLevel::SERIALIZABLE_ISOLATION:
      return is_for_write
          ? IntentTypeSet({IntentType::kStrongWrite}) : IntentTypeSet({IntentType::kStrongRead});
    case IsolationLevel::NON_TRANSACTIONAL:
      LOG(DFATAL) << "GetStrongIntentTypeSet invoked for non transactional isolation";
      return IntentTypeSet();
  }
  FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
}

inline IntentTypeSet GetIntentTypes(RowMarkType row_mark) {
  if (!IsValidRowMarkType(row_mark)) {
    return IntentTypeSet();
  }

  // Mapping of postgres locking levels to DocDB intent types is described in details by the
  // following comment https://github.com/yugabyte/yugabyte-db/issues/1199#issuecomment-501041018
  switch (row_mark) {
    case RowMarkType::ROW_MARK_EXCLUSIVE:
      // FOR UPDATE: strong read + strong write lock on the DocKey,
      //             as if we're replacing or deleting the entire row in DocDB.
      return IntentTypeSet({IntentType::kStrongRead, IntentType::kStrongWrite});
    case RowMarkType::ROW_MARK_NOKEYEXCLUSIVE:
      // FOR NO KEY UPDATE: strong read + weak write lock on the DocKey, as if we're reading
      //                    the entire row and then writing only a subset of columns in DocDB.
      return IntentTypeSet({IntentType::kStrongRead, IntentType::kWeakWrite});
    case RowMarkType::ROW_MARK_SHARE:
      // FOR SHARE: strong read on the DocKey, as if we're reading the entire row in DocDB.
      return IntentTypeSet({IntentType::kStrongRead});
    case RowMarkType::ROW_MARK_KEYSHARE:
      // FOR KEY SHARE: weak read lock on the DocKey, preventing the entire row from being
      //               replaced / deleted, as if we're simply reading some of the column.
      //               This is the type of locking that is used by foreign keys, so this will
      //               prevent the referenced row from disappearing. The reason it does not
      //               conflict with the FOR NO KEY UPDATE above is conceptually the following:
      //               an operation that reads the entire row and then writes a subset of columns
      //               (FOR NO KEY UPDATE) does not have to conflict with an operation that could
      //               be reading a different subset of columns (FOR KEY SHARE).
      return IntentTypeSet({IntentType::kWeakRead});

    case ROW_MARK_REFERENCE: [[fallthrough]];
    case ROW_MARK_COPY: [[fallthrough]];
    case ROW_MARK_ABSENT:
      // We shouldn't get here because other row lock types are disabled at the postgres level.
      LOG(DFATAL) << "Unsupported row lock of type " << RowMarkType_Name(row_mark);
      return IntentTypeSet();
  }

  FATAL_INVALID_ENUM_VALUE(RowMarkType, row_mark);
}

} // namespace

Status RemoveGroupEndSuffix(RefCntPrefix* key) {
  size_t size = DCHECK_NOTNULL(key)->size();
  if (size > 0) {
    if (key->data()[0] == KeyEntryTypeAsChar::kGroupEnd) {
      if (size != 1) {
        return STATUS_FORMAT(Corruption, "Key starting with group end: $0",
            key->as_slice().ToDebugHexString());
      }
      size = 0;
    } else {
      while (key->data()[size - 1] == KeyEntryTypeAsChar::kGroupEnd) {
        --size;
      }
    }
  }
  key->Resize(size);
  return Status::OK();
}

Result<DecodedIntentKey> DecodeIntentKey(const Slice &encoded_intent_key) {
  DecodedIntentKey result;
  auto& intent_prefix = result.intent_prefix;
  intent_prefix = encoded_intent_key;

  size_t doc_ht_size = VERIFY_RESULT(DocHybridTime::GetEncodedSize(intent_prefix));
  // There should always be 3 bytes present before teh start of the doc_ht:
  // 1. ValueType::kIntentTypeSet
  // 2. the corresponding value for ValueType::kIntentTypeSet
  // 3. ValueType::kHybridTime
  constexpr int kBytesBeforeDocHt = 3;
  if (intent_prefix.size() < doc_ht_size + kBytesBeforeDocHt) {
    return STATUS_FORMAT(
        Corruption, "Intent key is too short: $0 bytes", encoded_intent_key.size());
  }
  result.doc_ht.Assign(intent_prefix.Suffix(doc_ht_size));
  intent_prefix.remove_suffix(doc_ht_size + kBytesBeforeDocHt);
  auto* prefix_end = intent_prefix.end();

  if (prefix_end[2] != KeyEntryTypeAsChar::kHybridTime)
    return STATUS_FORMAT(Corruption, "Expecting hybrid time with ValueType $0, found $1",
        KeyEntryType::kHybridTime, static_cast<KeyEntryType>(prefix_end[2]));

  if (prefix_end[0] != KeyEntryTypeAsChar::kIntentTypeSet) {
    if (prefix_end[0] == KeyEntryTypeAsChar::kObsoleteIntentType) {
      result.intent_types = ObsoleteIntentTypeToSet(prefix_end[1]);
    } else if (prefix_end[0] == KeyEntryTypeAsChar::kObsoleteIntentTypeSet) {
      result.intent_types = ObsoleteIntentTypeSetToNew(prefix_end[1]);
    } else {
      return STATUS_FORMAT(
          Corruption,
          "Expecting intent type set ($0) or intent type ($1) or obsolete intent type set ($2), "
              "found $3",
          KeyEntryType::kIntentTypeSet, KeyEntryType::kObsoleteIntentType,
          KeyEntryType::kObsoleteIntentTypeSet, static_cast<KeyEntryType>(prefix_end[0]));
    }
  } else {
    result.intent_types = IntentTypeSet(prefix_end[1]);
  }

  return result;
}

Result<TransactionId> DecodeTransactionIdFromIntentValue(Slice* intent_value) {
  if (intent_value->empty()) {
    return STATUS_FORMAT(Corruption, "Expecting intent value to start with ValueType $0, but it is "
        "empty", ValueEntryType::kTransactionId);
  } else if (*intent_value->data() != ValueEntryTypeAsChar::kTransactionId) {
    return STATUS_FORMAT(Corruption, "Expecting intent key to start with ValueType $0, found $1",
        ValueEntryType::kTransactionId, static_cast<ValueEntryType>(*intent_value->data()));
  } else {
    intent_value->consume_byte();
  }
  return DecodeTransactionId(intent_value);
}

ReadIntentTypeSets GetIntentTypesForRead(IsolationLevel level, RowMarkType row_mark) {
  return {.read = GetIntentTypes(level), .row_mark = GetIntentTypes(row_mark)};
}

IntentTypeSet GetIntentTypesForWrite(IsolationLevel level) {
  return GetIntentTypes(level, /*is_for_write=*/true);
}

bool HasStrong(IntentTypeSet inp) {
  static const IntentTypeSet all_strong_intents{IntentType::kStrongRead, IntentType::kStrongWrite};
  return (inp & all_strong_intents).Any();
}

#define INTENT_VALUE_SCHECK(lhs, op, rhs, msg) \
  BOOST_PP_CAT(SCHECK_, op)(lhs, \
                            rhs, \
                            Corruption, \
                            Format("Bad intent value, $0 in $1, transaction: $2", \
                                   msg, \
                                   encoded_intent_value.ToDebugHexString(), \
                                   transaction_id_slice.ToDebugHexString()))

Result<DecodedIntentValue> DecodeIntentValue(
    const Slice& encoded_intent_value, const Slice* verify_transaction_id_slice,
    bool require_write_id) {
  DecodedIntentValue decoded_value;
  auto intent_value = encoded_intent_value;
  auto transaction_id_slice = Slice();

  if (verify_transaction_id_slice) {
    transaction_id_slice = *verify_transaction_id_slice;
    RETURN_NOT_OK(intent_value.consume_byte(ValueEntryTypeAsChar::kTransactionId));
    INTENT_VALUE_SCHECK(intent_value.starts_with(transaction_id_slice), EQ, true,
        "wrong transaction id");
    intent_value.remove_prefix(TransactionId::StaticSize());
  } else {
    decoded_value.transaction_id = VERIFY_RESULT(DecodeTransactionIdFromIntentValue(&intent_value));
    transaction_id_slice = decoded_value.transaction_id.AsSlice();
  }

  if (intent_value.TryConsumeByte(ValueEntryTypeAsChar::kSubTransactionId)) {
    decoded_value.subtransaction_id = Load<SubTransactionId, BigEndian>(intent_value.data());
    intent_value.remove_prefix(sizeof(SubTransactionId));
  } else {
    decoded_value.subtransaction_id = kMinSubTransactionId;
  }

  if (intent_value.TryConsumeByte(ValueEntryTypeAsChar::kWriteId)) {
    INTENT_VALUE_SCHECK(intent_value.size(), GE, sizeof(IntraTxnWriteId), "write id expected");
    decoded_value.write_id = BigEndian::Load32(intent_value.data());
    intent_value.remove_prefix(sizeof(IntraTxnWriteId));
  } else {
    RSTATUS_DCHECK(
      !require_write_id, Corruption, "Expected IntraTxnWriteId in value, found: $0",
      intent_value.ToDebugHexString());
  }

  decoded_value.body = intent_value;

  return decoded_value;
}

IntentTypeSet ObsoleteIntentTypeToSet(uint8_t obsolete_intent_type) {
  constexpr int kWeakIntentFlag         = 0b000;
  constexpr int kStrongIntentFlag       = 0b001;
  constexpr int kWriteIntentFlag        = 0b010;
  constexpr int kSnapshotIntentFlag     = 0b100;

  // Actually we have only 2 types of obsolete intent types that could be present.
  // Strong and weak snapshot writes.
  if (obsolete_intent_type == (kStrongIntentFlag | kWriteIntentFlag | kSnapshotIntentFlag)) {
    return IntentTypeSet({IntentType::kStrongRead, IntentType::kStrongWrite});
  } else if (obsolete_intent_type == (kWeakIntentFlag | kWriteIntentFlag | kSnapshotIntentFlag)) {
    return IntentTypeSet({IntentType::kWeakRead, IntentType::kWeakWrite});
  }

  LOG(DFATAL) << "Unexpected obsolete intent type: " << static_cast<int>(obsolete_intent_type);

  return IntentTypeSet();
}

IntentTypeSet ObsoleteIntentTypeSetToNew(uint8_t obsolete_intent_type_set) {
  IntentTypeSet result;
  for (size_t idx = 0; idx != 4; ++idx) {
    if (obsolete_intent_type_set & (1 << idx)) {
      // We swap two bits in every index because their meanings have changed places between the
      // obsolete vs. new format.
      result.Set(static_cast<IntentType>(((idx >> 1) | (idx << 1)) & 3));
    }
  }
  return result;
}

bool IntentValueType(char ch) {
  return ch == KeyEntryTypeAsChar::kIntentTypeSet ||
         ch == KeyEntryTypeAsChar::kObsoleteIntentTypeSet ||
         ch == KeyEntryTypeAsChar::kObsoleteIntentType;
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
template<class Functor>
Status EnumerateWeakIntents(
    Slice key,
    const Functor& functor,
    KeyBytes* encoded_key_buffer,
    PartialRangeKeyIntents partial_range_key_intents) {

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
  RETURN_NOT_OK(functor(FullDocKey::kFalse, encoded_key_buffer));

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
    RETURN_NOT_OK(functor(FullDocKey(key[0] == KeyEntryTypeAsChar::kGroupEnd), encoded_key_buffer));

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
      RETURN_NOT_OK(functor(full_doc_key, encoded_key_buffer));
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
    RETURN_NOT_OK(functor(FullDocKey::kTrue, encoded_key_buffer));
    subkey_start = key.cdata();
  }

  return STATUS(
      Corruption, "Expected to reach the end of the key after decoding last valid subkey");
}

}  // namespace

Status EnumerateIntents(
    Slice key, Slice intent_value, const EnumerateIntentsCallback& functor,
    KeyBytes* encoded_key_buffer, PartialRangeKeyIntents partial_range_key_intents,
    LastKey last_key) {
  static const Slice kRowLockValue{&dockv::ValueEntryTypeAsChar::kRowLock, 1};
  const IsRowLock is_row_lock{intent_value == kRowLockValue};
  RETURN_NOT_OK(EnumerateWeakIntents(
      key,
      [&functor, is_row_lock](FullDocKey full_doc_key, KeyBytes* encoded_key_buffer) {
        return functor(
            AncestorDocKey::kTrue,
            full_doc_key,
            Slice() /* intent_value */,
            encoded_key_buffer,
            LastKey::kFalse,
            is_row_lock);
      },
      encoded_key_buffer,
      partial_range_key_intents));
  return functor(
      AncestorDocKey::kFalse, FullDocKey::kTrue, intent_value, encoded_key_buffer,
      last_key, is_row_lock);
}

}  // namespace yb::dockv
