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

#include "yb/docdb/intent.h"

#include <string>

#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"

#include "yb/docdb/value_type.h"

#include "yb/gutil/endian.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace docdb {

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
        Corruption, "Intent key is too short: $0 bytes. Encoded key: $1",
        encoded_intent_key.size(), encoded_intent_key.ToDebugHexString());
  }
  result.doc_ht.Assign(intent_prefix.Suffix(doc_ht_size));
  intent_prefix.remove_suffix(doc_ht_size + kBytesBeforeDocHt);
  auto* prefix_end = intent_prefix.end();

  if (prefix_end[2] != KeyEntryTypeAsChar::kHybridTime)
    return STATUS_FORMAT(
        Corruption, "Expecting hybrid time with ValueType $0, found $1. Encoded key: $2",
        KeyEntryType::kHybridTime, static_cast<KeyEntryType>(prefix_end[2]),
        encoded_intent_key.ToDebugHexString());

  if (prefix_end[0] != KeyEntryTypeAsChar::kIntentTypeSet) {
    if (prefix_end[0] == KeyEntryTypeAsChar::kObsoleteIntentType) {
      result.intent_types = ObsoleteIntentTypeToSet(prefix_end[1]);
    } else if (prefix_end[0] == KeyEntryTypeAsChar::kObsoleteIntentTypeSet) {
      result.intent_types = ObsoleteIntentTypeSetToNew(prefix_end[1]);
    } else {
      return STATUS_FORMAT(
          Corruption,
          "Expecting intent type set ($0) or intent type ($1) or obsolete intent type set ($2), "
              "found $3. Encoded key: $4",
          KeyEntryType::kIntentTypeSet, KeyEntryType::kObsoleteIntentType,
          KeyEntryType::kObsoleteIntentTypeSet, static_cast<KeyEntryType>(prefix_end[0]),
          encoded_intent_key.ToDebugHexString());
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

IntentTypeSet AllStrongIntents() {
  return IntentTypeSet({IntentType::kStrongRead, IntentType::kStrongWrite});
}

IntentTypeSet GetStrongIntentTypeSet(
    IsolationLevel level,
    OperationKind operation_kind,
    RowMarkType row_mark) {
  if (IsValidRowMarkType(row_mark)) {
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
      default:
        // We shouldn't get here because other row lock types are disabled at the postgres level.
        LOG(DFATAL) << "Unsupported row lock of type " << RowMarkType_Name(row_mark);
        break;
    }
  }

  switch (level) {
    case IsolationLevel::READ_COMMITTED:
    case IsolationLevel::SNAPSHOT_ISOLATION:
      return IntentTypeSet({IntentType::kStrongRead, IntentType::kStrongWrite});
    case IsolationLevel::SERIALIZABLE_ISOLATION:
      switch (operation_kind) {
        case OperationKind::kRead:
          return IntentTypeSet({IntentType::kStrongRead});
        case OperationKind::kWrite:
          return IntentTypeSet({IntentType::kStrongWrite});
      }
      FATAL_INVALID_ENUM_VALUE(OperationKind, operation_kind);
    case IsolationLevel::NON_TRANSACTIONAL:
      LOG(DFATAL) << "GetStrongIntentTypeSet invoked for non transactional isolation";
      return IntentTypeSet();
  }
  FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
}

bool HasStrong(IntentTypeSet inp) {
  return (inp & AllStrongIntents()).Any();
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

}  // namespace docdb
}  // namespace yb
