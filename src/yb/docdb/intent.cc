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

#include <glog/logging.h>

#include "yb/common/transaction.h"
#include "yb/docdb/value_type.h"

namespace yb {
namespace docdb {

Status DecodeIntentKey(const Slice& encoded_intent_key, Slice* intent_prefix,
                       IntentType* intent_type, DocHybridTime* doc_ht) {
  *intent_prefix = encoded_intent_key;

  int doc_ht_size = 0;
  RETURN_NOT_OK(DocHybridTime::CheckAndGetEncodedSize(*intent_prefix, &doc_ht_size));
  if (intent_prefix->size() < doc_ht_size + 3) {
    return STATUS_FORMAT(
        Corruption, "Intent key is too short: $0 bytes", encoded_intent_key.size());
  }
  intent_prefix->remove_suffix(doc_ht_size + 3);
  if (doc_ht) {
    RETURN_NOT_OK(doc_ht->FullyDecodeFrom(
        Slice(intent_prefix->data() + intent_prefix->size() + 3, doc_ht_size)));
  }
  if (static_cast<ValueType>(intent_prefix->end()[2]) != ValueType::kHybridTime)
    return STATUS_FORMAT(Corruption, "Expecting hybrid time with ValueType $0, found $1",
        ValueType::kHybridTime, static_cast<ValueType>(intent_prefix->end()[2]));

  if (static_cast<ValueType>(intent_prefix->end()[0]) != ValueType::kIntentType)
    return STATUS_FORMAT(Corruption, "Expecting intent type with ValueType $0, found $1",
        ValueType::kIntentType, static_cast<ValueType>(intent_prefix->end()[0]));

  if (intent_type) {
    *intent_type = static_cast<IntentType>(intent_prefix->end()[1]);
  }

  return Status::OK();
}

Result<TransactionId> DecodeTransactionIdFromIntentValue(Slice* intent_value) {
  if (intent_value->empty()) {
    return STATUS_FORMAT(Corruption, "Expecting intent value to start with ValueType $0, but it is "
        "empty", ValueType::kTransactionId);
  } else if (*intent_value->data() != ValueTypeAsChar::kTransactionId) {
    return STATUS_FORMAT(Corruption, "Expecting intent key to start with ValueType $0, found $1",
        ValueType::kTransactionId, static_cast<ValueType>(*intent_value->data()));
  } else {
    intent_value->consume_byte();
  }
  return DecodeTransactionId(intent_value);
}

IntentTypePair GetWriteIntentsForIsolationLevel(IsolationLevel level) {
  switch(level) {
    case IsolationLevel::SNAPSHOT_ISOLATION:
      return { docdb::IntentType::kStrongSnapshotWrite,
               docdb::IntentType::kWeakSnapshotWrite };
    case IsolationLevel::SERIALIZABLE_ISOLATION:
      return { docdb::IntentType::kStrongSerializableWrite,
               docdb::IntentType::kWeakSerializableWrite };
    case IsolationLevel::NON_TRANSACTIONAL:
      FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
  }
  FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
}

#define INTENT_VALUE_SCHECK(lhs, op, rhs, msg) \
  BOOST_PP_CAT(SCHECK_, op)(lhs, \
                            rhs, \
                            Corruption, \
                            Format("Bad intent value, $0 in $1, transaction: $2", \
                                   msg, \
                                   encoded_intent_value.ToDebugHexString(), \
                                   transaction_id_slice.ToDebugHexString()))

CHECKED_STATUS DecodeIntentValue(
    const Slice& encoded_intent_value, const Slice& transaction_id_slice, IntraTxnWriteId* write_id,
    Slice* body) {
  Slice intent_value = encoded_intent_value;
  RETURN_NOT_OK(intent_value.consume_byte(ValueTypeAsChar::kTransactionId));
  INTENT_VALUE_SCHECK(intent_value.starts_with(transaction_id_slice), EQ, true,
      "wrong transaction id");
  intent_value.remove_prefix(TransactionId::static_size());

  RETURN_NOT_OK(intent_value.consume_byte(ValueTypeAsChar::kWriteId));
  INTENT_VALUE_SCHECK(intent_value.size(), GE, sizeof(IntraTxnWriteId), "write id expected");
  if (write_id) {
    *write_id = BigEndian::Load32(intent_value.data());
  }
  intent_value.remove_prefix(sizeof(IntraTxnWriteId));

  if (body) {
    *body = intent_value;
  }

  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
