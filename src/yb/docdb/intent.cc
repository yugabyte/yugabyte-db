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
  if (intent_prefix->empty()) {
    return STATUS_FORMAT(Corruption, "Expecting intent key to start with ValueType $0, but it is "
        "empty", ValueType::kIntentPrefix);
  } else if (*intent_prefix->data() != static_cast<char>(ValueType::kIntentPrefix)) {
    return STATUS_FORMAT(Corruption, "Expecting intent key to start with ValueType $0, found $1",
        ValueType::kIntentPrefix, static_cast<ValueType>(*intent_prefix->data()));
  }

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
  } else if (*intent_value->data() != static_cast<char>(ValueType::kTransactionId)) {
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
      return { docdb::IntentType::kStrongSerializableRead,
          docdb::IntentType::kWeakSerializableWrite };
    case IsolationLevel::NON_TRANSACTIONAL:
      FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
  }
  FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
}

Status Intent::DecodeFromKey(const rocksdb::Slice& encoded_intent) {
  Slice slice = encoded_intent;
  ValueType value_type = DecodeValueType(slice);

  if (value_type != ValueType::kGroupEnd) {
    return STATUS_FORMAT(Corruption, "Expecting intent to start with ValueType $0, found $1",
                         ValueType::kGroupEnd, value_type);
  }

  slice.consume_byte();

  RETURN_NOT_OK(subdoc_key_.DecodeFrom(&slice, HybridTimeRequired::kTrue));
  intent_type_ = static_cast<IntentType> (subdoc_key_.last_subkey().GetUInt16());
  subdoc_key_.RemoveLastSubKey();

  return Status::OK();
}

Status Intent::DecodeFromValue(const rocksdb::Slice& encoded_intent) {
  Slice slice = encoded_intent;
  ValueType value_type = DecodeValueType(slice);
  if (value_type != ValueType::kTransactionId) {
    return STATUS_FORMAT(Corruption, "Expecting ValueType $0, found $1",
        ValueType::kTransactionId, value_type);
  }
  slice.consume_byte();
  RETURN_NOT_OK(transaction_id_.DecodeFromComparableSlice(slice, kUuidSize));
  slice.remove_prefix(kUuidSize);
  return value_.Decode(slice);
}

string Intent::EncodeKey() {
  KeyBytes encoded_key;
  encoded_key.AppendValueType(ValueType::kGroupEnd);
  subdoc_key_.AppendSubKeysAndMaybeHybridTime(PrimitiveValue::IntentTypeValue(intent_type_));
  encoded_key.Append(subdoc_key_.Encode());
  subdoc_key_.RemoveLastSubKey();
  return encoded_key.AsStringRef();
}

string Intent::EncodeValue() const {
  string result = PrimitiveValue::TransactionId(transaction_id_).ToValue();
  value_.EncodeAndAppend(&result);
  return result;
}

string Intent::ToString() const {
  return yb::Format("Intent($0, $1, $2, $3)", subdoc_key_, intent_type_, transaction_id_, value_);
}


}  // namespace docdb
}  // namespace yb
