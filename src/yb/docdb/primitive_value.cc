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

#include "yb/docdb/primitive_value.h"

#include <string>

#include <glog/logging.h>

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/intent.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/compare_util.h"
#include "yb/util/decimal.h"
#include "yb/util/fast_varint.h"
#include "yb/util/net/inetaddress.h"

using std::string;
using strings::Substitute;
using yb::QLValuePB;
using yb::util::Decimal;
using yb::util::FormatBytesAsStr;
using yb::util::CompareUsingLessThan;
using yb::util::FastAppendSignedVarIntToStr;
using yb::util::FastDecodeSignedVarInt;

// We're listing all non-primitive value types at the end of switch statement instead of using a
// default clause so that we can ensure that we're handling all possible primitive value types
// at compile time.
#define IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH \
    case ValueType::kArray: FALLTHROUGH_INTENDED; \
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED; \
    case ValueType::kIntentPrefix: FALLTHROUGH_INTENDED; \
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED; \
    case ValueType::kObject: FALLTHROUGH_INTENDED; \
    case ValueType::kRedisSet: FALLTHROUGH_INTENDED; \
    case ValueType::kTtl: FALLTHROUGH_INTENDED; \
    case ValueType::kTombstone: \
      break

namespace yb {
namespace docdb {

namespace {

template <class T>
string DoubleToString(T val) {
  string s = std::to_string(val);
  // Remove trailing zeros.
  if (s.find(".") != string::npos) {
    s.erase(s.find_last_not_of('0') + 1, string::npos);
  }
  if (!s.empty() && s.back() == '.') {
    s += '0';
  }
  if (s == "0.0" && val != 0.0) {
    // Use the exponential notation for small numbers that would otherwise look like a zero.
    return StringPrintf("%E", val);
  }
  return s;
}

} // anonymous namespace

string PrimitiveValue::ToString() const {
  switch (type_) {
    case ValueType::kNull:
      return "null";
    case ValueType::kFalse:
      return "false";
    case ValueType::kTrue:
      return "true";
    case ValueType::kInvalidValueType:
      return "invalid";
    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kString:
      return FormatBytesAsStr(str_val_);
    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt32:
      return std::to_string(int32_val_);
    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64:
      return std::to_string(int64_val_);
    case ValueType::kFloat:
      return DoubleToString(float_val_);
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble:
      return DoubleToString(double_val_);
    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal: {
      util::Decimal decimal;
      auto status = decimal.DecodeFromComparable(decimal_val_);
      if (!status.ok()) {
        LOG(ERROR) << "Unable to decode decimal";
        return "";
      }
      return decimal.ToString();
    }
    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp:
      return timestamp_val_.ToString();
    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddress:
      return inetaddress_val_->ToString();
    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kUuid:
      return uuid_val_.ToString();
    case ValueType::kArrayIndex:
      return Substitute("ArrayIndex($0)", int64_val_);
    case ValueType::kHybridTime:
      return hybrid_time_val_.ToString();
    case ValueType::kUInt16Hash:
      return Substitute("UInt16Hash($0)", uint16_val_);
    case ValueType::kColumnId:
      return Substitute("ColumnId($0)", column_id_val_);
    case ValueType::kSystemColumnId:
      return Substitute("SystemColumnId($0)", column_id_val_);
    case ValueType::kObject:
      return "{}";
    case ValueType::kRedisSet:
      return "()";
    case ValueType::kTombstone:
      return "DEL";
    case ValueType::kArray:
      return "[]";
    case ValueType::kTransactionId:
      return Substitute("TransactionID($0)", uuid_val_.ToString());
    case ValueType::kIntentType:
      return Substitute("Intent($0)", docdb::ToString(static_cast<enum IntentType>(uint16_val_)));

    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kIntentPrefix:
      break;
    case ValueType::kLowest:
      return "-Inf";
    case ValueType::kHighest:
      return "+Inf";
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

void PrimitiveValue::AppendToKey(KeyBytes* key_bytes) const {
  key_bytes->AppendValueType(type_);
  switch (type_) {
    case ValueType::kLowest: return;
    case ValueType::kHighest: return;
    case ValueType::kNull: return;
    case ValueType::kFalse: return;
    case ValueType::kTrue: return;

    case ValueType::kString:
      key_bytes->AppendString(str_val_);
      return;

    case ValueType::kStringDescending:
      key_bytes->AppendDescendingString(str_val_);
      return;

    case ValueType::kInt64:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kInt32:
      key_bytes->AppendInt32(int32_val_);
      return;

    case ValueType::kInt32Descending:
      key_bytes->AppendDescendingInt32(int32_val_);
      return;

    case ValueType::kInt64Descending:
      key_bytes->AppendDescendingInt64(int64_val_);
      return;

    case ValueType::kDouble:
      key_bytes->AppendDouble(double_val_);
      return;

    case ValueType::kDoubleDescending:
      key_bytes->AppendDescendingDouble(double_val_);
      return;

    case ValueType::kFloat:
      key_bytes->AppendFloat(float_val_);
      return;

    case ValueType::kDecimal:
      key_bytes->AppendDecimal(decimal_val_);
      return;

    case ValueType::kDecimalDescending:
      key_bytes->AppendDecimalDescending(decimal_val_);
      return;

    case ValueType::kTimestamp:
      key_bytes->AppendInt64(timestamp_val_.ToInt64());
      return;

    case ValueType::kTimestampDescending:
      key_bytes->AppendDescendingInt64(timestamp_val_.ToInt64());
      return;

    case ValueType::kInetaddress: {
      std::string bytes;
      CHECK_OK(inetaddress_val_->ToBytes(&bytes));
      key_bytes->AppendString(bytes);
      return;
    }

    case ValueType::kInetaddressDescending: {
      std::string bytes;
      CHECK_OK(inetaddress_val_->ToBytes(&bytes));
      key_bytes->AppendDescendingString(bytes);
      return;
    }

    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      std::string bytes;
      CHECK_OK(uuid_val_.EncodeToComparable(&bytes));
      key_bytes->AppendString(bytes);
      return;
    }

    case ValueType::kUuidDescending: {
      std::string bytes;
      CHECK_OK(uuid_val_.EncodeToComparable(&bytes));
      key_bytes->AppendDescendingString(bytes);
      return;
    }

    case ValueType::kArrayIndex:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kHybridTime:
      hybrid_time_val_.AppendEncodedInDocDbFormat(key_bytes->mutable_data());
      return;

    case ValueType::kUInt16Hash:
      key_bytes->AppendUInt16(uint16_val_);
      return;

    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId:
      key_bytes->AppendColumnId(column_id_val_);
      return;

    case ValueType::kIntentType:
      key_bytes->AppendIntentType(static_cast<IntentType>(uint16_val_));
      return;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

string PrimitiveValue::ToValue() const {
  string result;
  result.push_back(static_cast<char>(type_));
  switch (type_) {
    case ValueType::kNull: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kTombstone: FALLTHROUGH_INTENDED;
    case ValueType::kObject: FALLTHROUGH_INTENDED;
    case ValueType::kArray: FALLTHROUGH_INTENDED;
    case ValueType::kRedisSet: return result;

    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kString:
      // No zero encoding necessary when storing the string in a value.
      result.append(str_val_);
      return result;

    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt32:
      AppendBigEndianUInt32(int32_val_, &result);
      return result;

    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64:
      AppendBigEndianUInt64(int64_val_, &result);
      return result;

    case ValueType::kArrayIndex:
      LOG(FATAL) << "Array index cannot be stored in a value";
      return result;

    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble:
      static_assert(sizeof(double) == sizeof(uint64_t),
                    "Expected double to be the same size as uint64_t");
      // TODO: make sure this is a safe and reasonable representation for doubles.
      AppendBigEndianUInt64(int64_val_, &result);
      return result;

    case ValueType::kFloat:
      static_assert(sizeof(float) == sizeof(uint32_t),
                    "Expected float to be the same size as uint32_t");
      // TODO: make sure this is a safe and reasonable representation for floats.
      AppendBigEndianUInt32(int32_val_, &result);
      return result;

    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal:
      result.append(decimal_val_);
      return result;

    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp:
      AppendBigEndianUInt64(timestamp_val_.ToInt64(), &result);
      return result;

    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddress: {
      std::string bytes;
      CHECK_OK(inetaddress_val_->ToBytes(&bytes))
      result.append(bytes);
      return result;
    }

    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      std::string bytes;
      CHECK_OK(uuid_val_.EncodeToComparable(&bytes))
      result.append(bytes);
      return result;
    }

    case ValueType::kUInt16Hash:
      // Hashes are not allowed in a value.
      break;

    case ValueType::kIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kIntentPrefix: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kHybridTime: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest:
      break;
  }

  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

Status PrimitiveValue::DecodeFromKey(rocksdb::Slice* slice) {
  return DecodeKey(slice, this);
}

Status PrimitiveValue::DecodeKey(rocksdb::Slice* slice, PrimitiveValue* out) {
  // A copy for error reporting.
  const rocksdb::Slice input_slice(*slice);

  if (slice->empty()) {
    return STATUS_SUBSTITUTE(Corruption,
        "Cannot decode a primitive value in the key encoding format from an empty slice: $0",
        ToShortDebugStr(input_slice));
  }
  ValueType value_type = ConsumeValueType(slice);
  ValueType dummy_type;
  ValueType& type_ref = out ? out->type_ : dummy_type;

  if (out) {
    out->~PrimitiveValue();
    // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
    // due to inability to allocate memory.
  }
  type_ref = ValueType::kNull;

  switch (value_type) {
    case ValueType::kNull: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kHighest: FALLTHROUGH_INTENDED;
    case ValueType::kLowest:
      type_ref = value_type;
      return Status::OK();

    case ValueType::kStringDescending: {
      if (out) {
        string result;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &result));
        new (&out->str_val_) string(std::move(result));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      // Only set type to string after string field initialization succeeds.
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kString: {
      if (out) {
        string result;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &result));
        new (&out->str_val_) string(std::move(result));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      // Only set type to string after string field initialization succeeds.
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal: {
      util::Decimal decimal;
      Slice slice_temp(slice->data(), slice->size());
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice_temp, &num_decoded_bytes));
      if (value_type == ValueType::kDecimalDescending) {
        // When we encode a descending decimal, we do a bitwise negation of each byte, which changes
        // the sign of the number. This way we reverse the sorting order. decimal.Negate() restores
        // the original sign of the number.
        decimal.Negate();
      }
      if (out) { // TODO avoid using temp variable, when out is nullptr
        new(&out->decimal_val_) string(decimal.EncodeToComparable());
      }
      slice->remove_prefix(num_decoded_bytes);
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt32:
      if (slice->size() < sizeof(int32_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 32-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->int32_val_ = BigEndian::Load32(slice->data()) ^ kInt32SignBitFlipMask;
        if (value_type == ValueType::kInt32Descending) {
          out->int32_val_ = ~out->int32_val_;
        }
      }
      slice->remove_prefix(sizeof(int32_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex:
      if (slice->size() < sizeof(int64_t)) {
        return STATUS_SUBSTITUTE(Corruption,
            "Not enough bytes to decode a 64-bit integer: $0",
            slice->size());
      }
      if (out) {
        out->int64_val_ = DecodeInt64FromKey(*slice);
        if (value_type == ValueType::kInt64Descending) {
          out->int64_val_ = ~out->int64_val_;
        }
      }
      slice->remove_prefix(sizeof(int64_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kUInt16Hash:
      if (slice->size() < sizeof(uint16_t)) {
        return STATUS(Corruption, Substitute("Not enough bytes to decode a 16-bit hash: $0",
                                             slice->size()));
      }
      if (out) {
        out->uint16_val_ = BigEndian::Load16(slice->data());
      }
      slice->remove_prefix(sizeof(uint16_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp: {
      if (slice->size() < sizeof(Timestamp)) {
        return STATUS(Corruption,
            Substitute("Not enough bytes to decode a Timestamp: $0, need $1",
                slice->size(), sizeof(Timestamp)));
      }
      if (out) {
        const auto uint64_timestamp = DecodeInt64FromKey(*slice);
        if (value_type == ValueType::kTimestampDescending) {
          // Flip all the bits after loading the integer.
          out->timestamp_val_ = Timestamp(~uint64_timestamp);
        } else {
          out->timestamp_val_ = Timestamp(uint64_timestamp);
        }
      }
      slice->remove_prefix(sizeof(Timestamp));
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kInetaddress: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &bytes));
        out->inetaddress_val_ = new InetAddress();
        RETURN_NOT_OK(out->inetaddress_val_->FromBytes(bytes));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kInetaddressDescending: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &bytes));
        out->inetaddress_val_ = new InetAddress();
        RETURN_NOT_OK(out->inetaddress_val_->FromBytes(bytes));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &bytes));
        new(&out->uuid_val_) Uuid();
        RETURN_NOT_OK(out->uuid_val_.DecodeFromComparable(bytes));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kUuidDescending: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &bytes));
        new(&out->uuid_val_) Uuid();
        RETURN_NOT_OK(out->uuid_val_.DecodeFromComparable(bytes));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: {
      // Decode varint
      int num_bytes_in_encoded_varint = 0;  // TODO: switch to size_t;
      {
        int64_t column_id_as_int64 = 0;
        ColumnId dummy_column_id;
        ColumnId& column_id_ref = out ? out->column_id_val_ : dummy_column_id;
        RETURN_NOT_OK(FastDecodeSignedVarInt(slice->data(), slice->size(), &column_id_as_int64 ,
                                             &num_bytes_in_encoded_varint));
        RETURN_NOT_OK(ColumnId::FromInt64(column_id_as_int64, &column_id_ref));
      }

      slice->remove_prefix(num_bytes_in_encoded_varint);
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kHybridTime: {
      if (out) {
        new(&out->hybrid_time_val_) DocHybridTime();
        RETURN_NOT_OK(out->hybrid_time_val_.DecodeFrom(slice));
      } else {
        DocHybridTime dummy_hybrid_time;
        RETURN_NOT_OK(dummy_hybrid_time.DecodeFrom(slice));
      }

      type_ref = ValueType::kHybridTime;
      return Status::OK();
    }

    case ValueType::kIntentType: {
      if (out) {
        out->uint16_val_ = static_cast<uint16_t>(*slice->data());
      }
      type_ref = value_type;
      slice->consume_byte();
      return Status::OK();
    }

    case ValueType::kFloat: {
      if (slice->size() < sizeof(float_t)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a float: $0", slice->size());
      }
      if (out) {
        out->float_val_ = DecodeFloatFromKey(*slice);
      }
      slice->remove_prefix(sizeof(float_t));
      type_ref = value_type;
      return Status::OK();
    }
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble: {
      if (slice->size() < sizeof(double_t)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a float: $0", slice->size());
      }
      if (out) {
        if (value_type == ValueType::kDoubleDescending) {
          out->double_val_ = DecodeDoubleFromKey(*slice, /* descending */ true);
        } else {
          out->double_val_ = DecodeDoubleFromKey(*slice);
        }
      }
      slice->remove_prefix(sizeof(double_t));
      type_ref = value_type;
      return Status::OK();
    }

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  return STATUS_FORMAT(
      Corruption,
      "Cannot decode value type $0 from the key encoding format: $1",
      value_type,
      ToShortDebugStr(input_slice));
}

Status PrimitiveValue::DecodeFromValue(const rocksdb::Slice& rocksdb_slice) {
  if (rocksdb_slice.empty()) {
    return STATUS(Corruption, "Cannot decode a value from an empty slice");
  }
  rocksdb::Slice slice(rocksdb_slice);
  this->~PrimitiveValue();
  // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
  // due to inability to allocate memory.
  type_ = ValueType::kNull;

  const auto value_type = ConsumeValueType(&slice);

  // TODO: ensure we consume all data from the given slice.
  switch (value_type) {
    case ValueType::kNull: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kObject: FALLTHROUGH_INTENDED;
    case ValueType::kArray: FALLTHROUGH_INTENDED;
    case ValueType::kRedisSet: FALLTHROUGH_INTENDED;
    case ValueType::kTombstone:
      type_ = value_type;
      complex_data_structure_ = nullptr;
      return Status::OK();

    case ValueType::kString:
      new(&str_val_) string(slice.cdata(), slice.size());
      // Only set type to string after string field initialization succeeds.
      type_ = ValueType::kString;
      return Status::OK();

    case ValueType::kInt32: FALLTHROUGH_INTENDED;
    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kFloat:
      if (slice.size() != sizeof(int32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      int32_val_ = BigEndian::Load32(slice.data());
      return Status::OK();

    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex: FALLTHROUGH_INTENDED;
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble:
      if (slice.size() != sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      int64_val_ = BigEndian::Load64(slice.data());
      return Status::OK();

    case ValueType::kDecimal: {
      util::Decimal decimal;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      type_ = value_type;
      new(&decimal_val_) string(decimal.EncodeToComparable());
      return Status::OK();
    }

    case ValueType::kTimestamp:
      if (slice.size() != sizeof(Timestamp)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      timestamp_val_ = Timestamp(BigEndian::Load64(slice.data()));
      return Status::OK();

    case ValueType::kInetaddress: {
      if (slice.size() != kInetAddressV4Size && slice.size() != kInetAddressV6Size) {
        return STATUS_FORMAT(Corruption,
            "Invalid number of bytes to decode IPv4/IPv6: $0, need $1 or $2",
            slice.size(), kInetAddressV4Size, kInetAddressV6Size);
      }
      // Need to use a non-rocksdb slice for InetAddress.
      Slice slice_temp(slice.data(), slice.size());
      inetaddress_val_ = new InetAddress();
      RETURN_NOT_OK(inetaddress_val_->FromSlice(slice_temp));
      type_ = value_type;
      return Status::OK();
    }

    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      if (slice.size() != kUuidSize) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes to decode Uuid: $0, need $1",
            slice.size(), kUuidSize);
      }
      Slice slice_temp(slice.data(), slice.size());
      new(&uuid_val_) Uuid();
      RETURN_NOT_OK(uuid_val_.DecodeFromComparableSlice(slice_temp));
      type_ = value_type;
      return Status::OK();
    }

    case ValueType::kIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kIntentPrefix: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kHybridTime: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest:
      return STATUS_FORMAT(Corruption, "$0 is not allowed in a RocksDB PrimitiveValue", value_type);
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
  return Status::OK();
}

PrimitiveValue PrimitiveValue::Double(double d, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kAscending) {
    primitive_value.type_ = ValueType::kDouble;
  } else {
    primitive_value.type_ = ValueType::kDoubleDescending;
  }
  primitive_value.double_val_ = d;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Float(float f) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kFloat;
  primitive_value.float_val_ = f;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Decimal(const string& encoded_decimal_str, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kDecimalDescending;
  } else {
    primitive_value.type_ = ValueType::kDecimal;
  }
  new(&primitive_value.decimal_val_) string(encoded_decimal_str);
  return primitive_value;
}

PrimitiveValue PrimitiveValue::ArrayIndex(int64_t index) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kArrayIndex;
  primitive_value.int64_val_ = index;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::UInt16Hash(uint16_t hash) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kUInt16Hash;
  primitive_value.uint16_val_ = hash;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::SystemColumnId(SystemColumnIds system_column_id) {
  return PrimitiveValue::SystemColumnId(ColumnId(static_cast<ColumnIdRep>(system_column_id)));
}

PrimitiveValue PrimitiveValue::SystemColumnId(ColumnId column_id) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kSystemColumnId;
  primitive_value.column_id_val_ = column_id;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Int32(int32_t v, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kInt32Descending;
  } else {
    primitive_value.type_ = ValueType::kInt32;
  }
  primitive_value.int32_val_ = v;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::TransactionId(Uuid transaction_id) {
  PrimitiveValue primitive_value(transaction_id);
  primitive_value.type_ = ValueType::kTransactionId;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::IntentTypeValue(IntentType intent_type) {
  PrimitiveValue primitive_value(static_cast<uint16_t>(intent_type));
  primitive_value.type_ = ValueType::kIntentType;
  return primitive_value;
}


KeyBytes PrimitiveValue::ToKeyBytes() const {
  KeyBytes kb;
  AppendToKey(&kb);
  return kb;
}

bool PrimitiveValue::operator==(const PrimitiveValue& other) const {
  if (type_ != other.type_) {
    return false;
  }
  switch (type_) {
    case ValueType::kNull: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest: return true;

    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kString: return str_val_ == other.str_val_;

    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt32: return int32_val_ == other.int32_val_;

    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex: return int64_val_ == other.int64_val_;

    case ValueType::kFloat: {
      if (util::IsNanFloat(float_val_) && util::IsNanFloat(other.float_val_)) {
        return true;
      }
      return float_val_ == other.float_val_;
    }
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble: {
      if (util::IsNanDouble(double_val_) && util::IsNanDouble(other.double_val_)) {
        return true;
      }
      return double_val_ == other.double_val_;
    }
    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal: return decimal_val_ == other.decimal_val_;
    case ValueType::kIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash: return uint16_val_ == other.uint16_val_;

    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp: return timestamp_val_ == other.timestamp_val_;
    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddress: return *inetaddress_val_ == *(other.inetaddress_val_);
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: return uuid_val_ == other.uuid_val_;

    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: return column_id_val_ == other.column_id_val_;
    case ValueType::kHybridTime: return hybrid_time_val_.CompareTo(other.hybrid_time_val_) == 0;
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

int PrimitiveValue::CompareTo(const PrimitiveValue& other) const {
  int result = CompareUsingLessThan(type_, other.type_);
  if (result != 0) {
    return result;
  }
  switch (type_) {
    case ValueType::kNull: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest:
      return 0;
    case ValueType::kStringDescending:
      return other.str_val_.compare(str_val_);
    case ValueType::kString:
      return str_val_.compare(other.str_val_);
    case ValueType::kInt64Descending:
      return CompareUsingLessThan(other.int64_val_, int64_val_);
    case ValueType::kInt32Descending:
      return CompareUsingLessThan(other.int32_val_, int32_val_);
    case ValueType::kInt32:
      return CompareUsingLessThan(int32_val_, other.int32_val_);
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex:
      return CompareUsingLessThan(int64_val_, other.int64_val_);
    case ValueType::kDoubleDescending:
      return CompareUsingLessThan(other.double_val_, double_val_);
    case ValueType::kDouble:
      return CompareUsingLessThan(double_val_, other.double_val_);
    case ValueType::kFloat:
      return CompareUsingLessThan(float_val_, other.float_val_);
    case ValueType::kDecimalDescending:
      return other.decimal_val_.compare(decimal_val_);
    case ValueType::kDecimal:
      return decimal_val_.compare(other.decimal_val_);
    case ValueType::kIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash:
      return CompareUsingLessThan(uint16_val_, other.uint16_val_);
    case ValueType::kTimestampDescending:
      return CompareUsingLessThan(other.timestamp_val_, timestamp_val_);
    case ValueType::kTimestamp:
      return CompareUsingLessThan(timestamp_val_, other.timestamp_val_);
    case ValueType::kInetaddressDescending:
      return CompareUsingLessThan(*(other.inetaddress_val_), *inetaddress_val_);
    case ValueType::kInetaddress:
      return CompareUsingLessThan(*inetaddress_val_, *(other.inetaddress_val_));
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUuidDescending:
      return CompareUsingLessThan(other.uuid_val_, uuid_val_);
    case ValueType::kUuid:
      return CompareUsingLessThan(uuid_val_, other.uuid_val_);
    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId:
      return CompareUsingLessThan(column_id_val_, other.column_id_val_);
    case ValueType::kHybridTime:
      // HybridTimes are sorted in reverse order when wrapped in a PrimitiveValue.
      return -hybrid_time_val_.CompareTo(other.hybrid_time_val_);
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  LOG(FATAL) << "Comparing invalid PrimitiveValues: " << *this << " and " << other;
}

// This is used to initialize kNull, kTrue, kFalse constants.
PrimitiveValue::PrimitiveValue(ValueType value_type)
    : type_(value_type) {
  if (value_type == ValueType::kString || value_type == ValueType::kStringDescending) {
    new(&str_val_) std::string();
  } else if (value_type == ValueType::kInetaddress
      || value_type == ValueType::kInetaddressDescending) {
    inetaddress_val_ = new InetAddress();
  } else if (value_type == ValueType::kDecimal || value_type == ValueType::kDecimalDescending) {
    new(&decimal_val_) std::string();
  } else if (value_type == ValueType::kUuid || value_type == ValueType::kUuidDescending) {
    new(&uuid_val_) Uuid();
  }
}

SortOrder PrimitiveValue::SortOrderFromColumnSchemaSortingType(
    ColumnSchema::SortingType sorting_type) {
  if (sorting_type == ColumnSchema::SortingType::kDescending) {
    return SortOrder::kDescending;
  }
  return SortOrder::kAscending;
}

PrimitiveValue PrimitiveValue::FromKuduValue(DataType data_type, Slice slice) {
  switch (data_type) {
    case DataType::INT64:
      return PrimitiveValue(*reinterpret_cast<const int64_t*>(slice.data()));
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::STRING:
      return PrimitiveValue(slice.ToString());
    case DataType::INT32:
      // TODO: fix cast when variable length integer encoding is implemented.
      return PrimitiveValue::Int32(*reinterpret_cast<const int32_t*>(slice.data()));
    case DataType::INT8:
      // TODO: fix cast when variable length integer encoding is implemented.
      return PrimitiveValue::Int32(*reinterpret_cast<const int8_t*>(slice.data()));
    case DataType::BOOL:
      // TODO(mbautin): check if this is the right way to interpret a bool value in Kudu.
      return PrimitiveValue(*slice.data() == 0 ? ValueType::kFalse: ValueType::kTrue);
    default:
      LOG(FATAL) << "Converting Kudu value of type " << data_type
                 << " to docdb PrimitiveValue is currently not supported";
    }
}

PrimitiveValue PrimitiveValue::FromQLValuePB(const QLValuePB& value,
                                              ColumnSchema::SortingType sorting_type) {
  const auto sort_order = SortOrderFromColumnSchemaSortingType(sorting_type);

  switch (value.value_case()) {
    case QLValuePB::kInt8Value:    return PrimitiveValue::Int32(QLValue::int8_value(value),
                                                                 sort_order);
    case QLValuePB::kInt16Value:   return PrimitiveValue::Int32(QLValue::int16_value(value),
                                                                 sort_order);
    case QLValuePB::kInt32Value:   return PrimitiveValue::Int32(QLValue::int32_value(value),
                                                                 sort_order);
    case QLValuePB::kInt64Value:   return PrimitiveValue(QLValue::int64_value(value), sort_order);
    case QLValuePB::kFloatValue: {
      if (sort_order != SortOrder::kAscending) {
        LOG(ERROR) << "Ignoring invalid sort order for FLOAT. Using SortOrder::kAscending.";
      }
      float f = QLValue::float_value(value);
      return PrimitiveValue::Float(util::CanonicalizeFloat(f));
    }
    case QLValuePB::kDoubleValue: {
      double d = QLValue::double_value(value);
      return PrimitiveValue::Double(util::CanonicalizeDouble(d), sort_order);
    }
    case QLValuePB::kDecimalValue:
      return PrimitiveValue::Decimal(QLValue::decimal_value(value), sort_order);
    case QLValuePB::kStringValue:
      return PrimitiveValue(QLValue::string_value(value), sort_order);
    case QLValuePB::kBinaryValue:
      // TODO consider using dedicated encoding for binary (not string) to avoid overhead of
      // zero-encoding for keys (since zero-bytes could be common for binary)
      return PrimitiveValue(QLValue::binary_value(value), sort_order);
    case QLValuePB::kBoolValue:
      if (sort_order != SortOrder::kAscending) {
        LOG(ERROR) << "Ignoring invalid sort order for BOOL. Using SortOrder::kAscending.";
      }
      return PrimitiveValue(QLValue::bool_value(value) ? ValueType::kTrue : ValueType::kFalse);
    case QLValuePB::kTimestampValue:
      return PrimitiveValue(QLValue::timestamp_value(value), sort_order);
    case QLValuePB::kInetaddressValue:
      return PrimitiveValue(QLValue::inetaddress_value(value), sort_order);
    case QLValuePB::kUuidValue:
      return PrimitiveValue(QLValue::uuid_value(value), sort_order);
    case QLValuePB::kTimeuuidValue:
      return PrimitiveValue(QLValue::timeuuid_value(value), sort_order);
    case QLValuePB::kFrozenValue:
      return PrimitiveValue(QLValue::frozen_value(value), sort_order);
    case QLValuePB::VALUE_NOT_SET:
      return PrimitiveValue(ValueType::kTombstone);

    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kVarintValue:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype in PrimitiveValue: " << value.value_case();
}

void PrimitiveValue::ToQLValuePB(const PrimitiveValue& primitive_value,
                                  const std::shared_ptr<QLType>& ql_type,
                                  QLValuePB* ql_value) {
  // DocDB sets type to kInvalidValueType for SubDocuments that don't exist. That's why they need
  // to be set to Null in QLValue.
  if (primitive_value.value_type() == ValueType::kNull ||
      primitive_value.value_type() == ValueType::kInvalidValueType) {
    QLValue::SetNull(ql_value);
    return;
  }

  switch (ql_type->main()) {
    case INT8:
      QLValue::set_int8_value(static_cast<int8_t>(primitive_value.GetInt32()), ql_value);
      return;
    case INT16:
      QLValue::set_int16_value(static_cast<int16_t>(primitive_value.GetInt32()), ql_value);
      return;
    case INT32:
      QLValue::set_int32_value(static_cast<int32_t>(primitive_value.GetInt32()), ql_value);
      return;
    case INT64:
      QLValue::set_int64_value(static_cast<int64_t>(primitive_value.GetInt64()), ql_value);
      return;
    case FLOAT:
      QLValue::set_float_value(static_cast<float>(primitive_value.GetFloat()), ql_value);
      return;
    case DOUBLE:
      QLValue::set_double_value(primitive_value.GetDouble(), ql_value);
      return;
    case DECIMAL:
      QLValue::set_decimal_value(primitive_value.GetDecimal(), ql_value);
      return;
    case BOOL:
      QLValue::set_bool_value((primitive_value.value_type() == ValueType::kTrue), ql_value);
      return;
    case TIMESTAMP:
      QLValue::set_timestamp_value(primitive_value.GetTimestamp(), ql_value);
      return;
    case INET:
      QLValue::set_inetaddress_value(*primitive_value.GetInetaddress(), ql_value);
      return;
    case UUID:
      QLValue::set_uuid_value(primitive_value.GetUuid(), ql_value);
      return;
    case TIMEUUID:
      QLValue::set_timeuuid_value(primitive_value.GetUuid(), ql_value);
      return;
    case STRING:
      QLValue::set_string_value(primitive_value.GetString(), ql_value);
      return;
    case BINARY:
      QLValue::set_binary_value(primitive_value.GetString(), ql_value);
      return;
    case FROZEN:
      QLValue::set_frozen_value(primitive_value.GetString(), ql_value);
      return;

    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case VARINT: FALLTHROUGH_INTENDED;
    case MAP: FALLTHROUGH_INTENDED;
    case SET: FALLTHROUGH_INTENDED;
    case LIST: FALLTHROUGH_INTENDED;
    case TUPLE: FALLTHROUGH_INTENDED;
    case TYPEARGS: FALLTHROUGH_INTENDED;
    case USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DATE: FALLTHROUGH_INTENDED;
    case TIME: FALLTHROUGH_INTENDED;

    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype " << ql_type->ToString();
}

PrimitiveValue PrimitiveValue::FromQLExpressionPB(const QLExpressionPB& ql_expr,
                                                   ColumnSchema::SortingType sorting_type) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:
      return FromQLValuePB(ql_expr.value(), sorting_type);
    case QLExpressionPB::ExprCase::kColumnId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kSubscriptedCol: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBfcall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kTscall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << ql_expr.expr_case();
}

void PrimitiveValue::ToQLExpressionPB(const PrimitiveValue& pv,
                                       const std::shared_ptr<QLType>& ql_type,
                                       QLExpressionPB* ql_expr) {
  ToQLValuePB(pv, ql_type, ql_expr->mutable_value());
}

}  // namespace docdb
}  // namespace yb
