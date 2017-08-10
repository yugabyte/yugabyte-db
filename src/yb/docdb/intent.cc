// Copyright (c) YugaByte, Inc.

#include "yb/docdb/intent.h"

#include <string>

#include <glog/logging.h>
#include "yb/docdb/value_type.h"

namespace yb {
namespace docdb {

Status Intent::DecodeFromKey(const rocksdb::Slice& encoded_intent) {
  Slice slice = encoded_intent;
  ValueType value_type = DecodeValueType(slice);

  if (value_type != ValueType::kGroupEnd) {
    return STATUS_FORMAT(Corruption, "Expecting intent to start with ValueType $0, found $1",
                         ValueType::kGroupEnd, value_type);
  }

  slice.consume_byte();

  RETURN_NOT_OK(subdoc_key_.DecodeFrom(&slice, /* require hybrid time */ true));
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
  encoded_key.Append(subdoc_key_.Encode(/* include hybrid time */ true));
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
