// Copyright (c) YugaByte, Inc.

#include "yb/docdb/primitive_value.h"

#include <string>

#include <glog/logging.h>

#include "yb/docdb/doc_kv_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/bytes_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"

using std::string;
using strings::Substitute;
using yb::YQLValuePB;
using yb::util::FormatBytesAsStr;

// We're listing all non-primitive value types at the end of switch statement instead of using a
// default clause so that we can ensure that we're handling all possible primitive value types
// at compile time.
#define IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH \
    case ValueType::kArray: FALLTHROUGH_INTENDED; \
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED; \
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED; \
    case ValueType::kObject: FALLTHROUGH_INTENDED; \
    case ValueType::kTtl: FALLTHROUGH_INTENDED; \
    case ValueType::kTombstone: \
      break

namespace yb {
namespace docdb {

string PrimitiveValue::ToString() const {
  switch (type_) {
    case ValueType::kNull:
      return "null";
    case ValueType::kFalse:
      return "false";
    case ValueType::kTrue:
      return "true";
    case ValueType::kString:
      return FormatBytesAsStr(str_val_);
    case ValueType::kInt64:
      return std::to_string(int64_val_);
    case ValueType::kDouble: {
      string s = std::to_string(double_val_);
      // Remove trailing zeros.
      if (s.find(".") != string::npos) {
        s.erase(s.find_last_not_of('0') + 1, string::npos);
      }
      if (!s.empty() && s.back() == '.') {
        s += '0';
      }
      if (s == "0.0" && double_val_ != 0.0) {
        // Use the exponential notation for small numbers that would otherwise look like a zero.
        return StringPrintf("%E", double_val_);
      }
      return s;
    }
    case ValueType::kTimestamp:
      return timestamp_val_.ToString();
    case ValueType::kArrayIndex:
      return Substitute("ArrayIndex($0)", int64_val_);
    case ValueType::kHybridTime:
      // TODO: print out hybrid_times in a human-readable way?
      return hybrid_time_val_.ToDebugString();
    case ValueType::kUInt16Hash:
      return Substitute("UInt16Hash($0)", uint16_val_);
    case ValueType::kObject:
      return "{}";
    case ValueType::kTombstone:
      return "DEL";
    case ValueType::kArray:
      return "[]";

    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kTtl:
      break;
  }
  LOG(FATAL) << __FUNCTION__ << " not implemented for value type " << ValueTypeToStr(type_);
}

void PrimitiveValue::AppendToKey(KeyBytes* key_bytes) const {
  key_bytes->AppendValueType(type_);
  switch (type_) {
    case ValueType::kNull: return;
    case ValueType::kFalse: return;
    case ValueType::kTrue: return;

    case ValueType::kString:
      key_bytes->AppendString(str_val_);
      return;

    case ValueType::kInt64:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kDouble:
      LOG(FATAL) << "Double cannot be used as a key";
      return;

    case ValueType::kTimestamp:
      key_bytes->AppendInt64(timestamp_val_.ToInt64());
      return;

    case ValueType::kArrayIndex:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kHybridTime:
      key_bytes->AppendHybridTime(hybrid_time_val_);
      return;

    case ValueType::kUInt16Hash:
      key_bytes->AppendUInt16(uint16_val_);
      return;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  LOG(FATAL) << __FUNCTION__ << " not implemented for value type " << ValueTypeToStr(type_);
}

string PrimitiveValue::ToValue() const {
  string result;
  result.push_back(static_cast<char>(type_));
  switch (type_) {
    case ValueType::kNull: return result;
    case ValueType::kFalse: return result;
    case ValueType::kTrue: return result;
    case ValueType::kTombstone: return result;
    case ValueType::kObject: return result;

    case ValueType::kString:
      // No zero encoding necessary when storing the string in a value.
      result.append(str_val_);
      return result;

    case ValueType::kInt64:
      AppendBigEndianUInt64(int64_val_, &result);
      return result;

    case ValueType::kArrayIndex:
      LOG(FATAL) << "Array index cannot be stored in a value";
      return result;

    case ValueType::kDouble:
      static_assert(sizeof(double) == sizeof(uint64_t),
                    "Expected double to be the same size as uint64_t");
      // TODO: make sure this is a safe and reasonable representation for doubles.
      AppendBigEndianUInt64(int64_val_, &result);
      return result;

    case ValueType::kTimestamp:
      AppendBigEndianUInt64(timestamp_val_.ToInt64(), &result);
      return result;

    case ValueType::kHybridTime:
      AppendBigEndianUInt64(hybrid_time_val_.value(), &result);
      return result;

    case ValueType::kUInt16Hash:
      // Hashes are not allowed in a value.
      break;

    case ValueType::kArray: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType:
      break;
  }

  LOG(FATAL) << __FUNCTION__ << " not implemented for value type " << ValueTypeToStr(type_);
}

Status PrimitiveValue::DecodeFromKey(rocksdb::Slice* slice) {
  // A copy for error reporting.
  const rocksdb::Slice input_slice(*slice);

  if (slice->empty()) {
    return STATUS_SUBSTITUTE(Corruption,
        "Cannot decode a primitive value in the key encoding format from an empty slice: $0",
        ToShortDebugStr(input_slice));
  }
  ValueType value_type = ConsumeValueType(slice);

  this->~PrimitiveValue();
  // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
  // due to inability to allocate memory.
  type_ = ValueType::kNull;

  switch (value_type) {
    case ValueType::kNull: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue:
      type_ = value_type;
      return Status::OK();

    case ValueType::kString: {
      string result;
      RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &result));
      new(&str_val_) string(result);
      // Only set type to string after string field initialization succeeds.
      type_ = ValueType::kString;
      return Status::OK();
    }

    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex:
      if (slice->size() < sizeof(int64_t)) {
        return STATUS_SUBSTITUTE(Corruption,
            "Not enough bytes to decode a 64-bit integer: $0",
            slice->size());
      }
      int64_val_ = BigEndian::Load64(slice->data()) ^ kInt64SignBitFlipMask;
      slice->remove_prefix(sizeof(int64_t));
      type_ = value_type;
      return Status::OK();

    case ValueType::kUInt16Hash:
      if (slice->size() < sizeof(uint16_t)) {
        return STATUS(Corruption, Substitute("Not enough bytes to decode a 16-bit hash: $0",
                                             slice->size()));
      }
      uint16_val_ = BigEndian::Load16(slice->data());
      slice->remove_prefix(sizeof(uint16_t));
      type_ = value_type;
      return Status::OK();

    case ValueType::kTimestamp:
      if (slice->size() < sizeof(Timestamp)) {
        return STATUS(Corruption,
            Substitute("Not enough bytes to decode a Timestamp: $0, need $1",
                slice->size(), sizeof(Timestamp)));
      }
      timestamp_val_ = Timestamp(BigEndian::Load64(slice->data()) ^ kInt64SignBitFlipMask);
      slice->remove_prefix(sizeof(Timestamp));
      type_ = value_type;
      return Status::OK();

    case ValueType::kHybridTime:
      if (slice->size() < kBytesPerHybridTime) {
        return STATUS(Corruption,
            Substitute("Not enough bytes to decode a hybrid_time: $0, need $1",
                slice->size(), kBytesPerHybridTime));
      }
      hybrid_time_val_ = DecodeHybridTimeFromKey(*slice, /* pos = */ 0);
      slice->remove_prefix(kBytesPerHybridTime);
      type_ = value_type;
      return Status::OK();

    case ValueType::kDouble:
      // Doubles are not allowed in a key as of 07/15/2016.
      break;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  return STATUS(Corruption,
      Substitute("Cannot decode value type $0 from the key encoding format: $1",
          ValueTypeToStr(value_type),
          ToShortDebugStr(input_slice)));
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
    case ValueType::kTombstone:
      type_ = value_type;
      return Status::OK();

    case ValueType::kString:
      new(&str_val_) string(slice.data(), slice.size());
      // Only set type to string after string field initialization succeeds.
      type_ = ValueType::kString;
      return Status::OK();

    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex: FALLTHROUGH_INTENDED;
    case ValueType::kDouble:
      if (slice.size() != sizeof(int64_t)) {
        return STATUS(Corruption,
            Substitute("Invalid number of bytes for a $0: $1",
                ValueTypeToStr(value_type), slice.size()));
      }
      type_ = value_type;
      int64_val_ = BigEndian::Load64(slice.data());
      return Status::OK();

    case ValueType::kTimestamp:
      if (slice.size() != sizeof(Timestamp)) {
        return STATUS(Corruption,
            Substitute("Invalid number of bytes for a $0: $1",
                ValueTypeToStr(value_type), slice.size()));
      }
      type_ = value_type;
      timestamp_val_ = Timestamp(BigEndian::Load64(slice.data()));
      return Status::OK();

    case ValueType::kArray:
      return STATUS(IllegalState, "Arrays are currently not supported");

    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kHybridTime:
      return STATUS(Corruption,
          Substitute("$0 is not allowed in a RocksDB PrimitiveValue", ValueTypeToStr(value_type)));
  }
  LOG(FATAL) << "Invalid value type: " << ValueTypeToStr(value_type);
  return Status::OK();
}

PrimitiveValue PrimitiveValue::Double(double d) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kDouble;
  primitive_value.double_val_ = d;
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
    case ValueType::kNull: return true;
    case ValueType::kFalse: return true;
    case ValueType::kTrue: return true;
    case ValueType::kString: return str_val_ == other.str_val_;

    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex: return int64_val_ == other.int64_val_;

    case ValueType::kDouble: return double_val_ == other.double_val_;
    case ValueType::kUInt16Hash: return uint16_val_ == other.uint16_val_;
    case ValueType::kTimestamp: return timestamp_val_ == other.timestamp_val_;
    case ValueType::kHybridTime: return hybrid_time_val_.CompareTo(other.hybrid_time_val_) == 0;
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  LOG(FATAL) << "Trying to test equality of wrong PrimitiveValue type: " << ValueTypeToStr(type_);
}

int PrimitiveValue::CompareTo(const PrimitiveValue& other) const {
  int result = GenericCompare(type_, other.type_);
  if (result != 0) {
    return result;
  }
  switch (type_) {
    case ValueType::kNull: return 0;
    case ValueType::kFalse: return 0;
    case ValueType::kTrue: return 0;
    case ValueType::kString:
      return str_val_.compare(other.str_val_);
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex:
      return GenericCompare(int64_val_, other.int64_val_);
    case ValueType::kDouble:
      return GenericCompare(double_val_, other.double_val_);
    case ValueType::kUInt16Hash:
      return GenericCompare(uint16_val_, other.uint16_val_);
    case ValueType::kTimestamp:
      return GenericCompare(timestamp_val_, other.timestamp_val_);
    case ValueType::kHybridTime:
      // HybridTimes are sorted in reverse order.
      return -GenericCompare(hybrid_time_val_.value(), other.hybrid_time_val_.value());
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  LOG(FATAL) << "Comparing invalid PrimitiveValues: " << *this << " and " << other;
}

// This is used to initialize kNull, kTrue, kFalse constants.
PrimitiveValue::PrimitiveValue(ValueType value_type)
    : type_(value_type) {
  if (value_type == ValueType::kString) {
    new(&str_val_) std::string();
  }
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
      return PrimitiveValue(*reinterpret_cast<const int32_t*>(slice.data()));
    case DataType::INT8:
      // TODO: fix cast when variable length integer encoding is implemented.
      return PrimitiveValue(*reinterpret_cast<const int8_t*>(slice.data()));
    case DataType::BOOL:
      // TODO(mbautin): check if this is the right way to interpret a bool value in Kudu.
      return PrimitiveValue(*slice.data() == 0 ? ValueType::kFalse: ValueType::kTrue);
    default:
      LOG(FATAL) << "Converting Kudu value of type " << data_type
                 << " to docdb PrimitiveValue is currently not supported";
    }
}

PrimitiveValue PrimitiveValue::FromYQLValuePB(const YQLValuePB& value) {
  CHECK(value.has_datatype());
  switch (value.datatype()) {
    case INT8:
      return value.has_int8_value() ?
          PrimitiveValue(value.int8_value()) : PrimitiveValue(ValueType::kNull);
    case INT16:
      return value.has_int16_value() ?
          PrimitiveValue(value.int16_value()) : PrimitiveValue(ValueType::kNull);
    case INT32:
      return value.has_int32_value() ?
          PrimitiveValue(value.int32_value()) : PrimitiveValue(ValueType::kNull);
    case INT64:
      return value.has_int64_value() ?
          PrimitiveValue(value.int64_value()) : PrimitiveValue(ValueType::kNull);
    case FLOAT:
      return value.has_float_value() ?
          PrimitiveValue::Double(value.float_value()) : PrimitiveValue(ValueType::kNull);
    case DOUBLE:
      return value.has_double_value() ?
          PrimitiveValue::Double(value.double_value()) : PrimitiveValue(ValueType::kNull);
    case STRING:
      return value.has_string_value() ?
          PrimitiveValue(value.string_value()) : PrimitiveValue(ValueType::kNull);
    case BOOL:
      return value.has_bool_value() ?
          PrimitiveValue(value.bool_value() ? ValueType::kTrue : ValueType::kFalse) :
          PrimitiveValue(ValueType::kNull);
    case TIMESTAMP:
      return value.has_timestamp_value() ?
          PrimitiveValue(Timestamp(value.timestamp_value())) : PrimitiveValue(ValueType::kNull);
    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype " << value.datatype();
}

PrimitiveValue PrimitiveValue::FromYQLValue(const YQLValue& value) {
  if (value.IsNull()) {
    return PrimitiveValue(ValueType::kNull);
  }

  switch (value.type()) {
    case INT8:
      return PrimitiveValue(value.int8_value());
    case INT16:
      return PrimitiveValue(value.int16_value());
    case INT32:
      return PrimitiveValue(value.int32_value());
    case INT64:
      return  PrimitiveValue(value.int64_value());
    case FLOAT:
      return PrimitiveValue::Double(value.float_value());
    case DOUBLE:
      return PrimitiveValue::Double(value.double_value());
    case STRING:
      return PrimitiveValue(value.string_value());
    case BOOL:
      return PrimitiveValue(value.bool_value() ? ValueType::kTrue : ValueType::kFalse);
    case TIMESTAMP:
      return PrimitiveValue(Timestamp(value.timestamp_value()));
    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype " << value.type();
}

void PrimitiveValue::ToYQLValue(YQLValue* v) const {
  if (value_type() == ValueType::kNull) {
    v->SetNull(true);
    return;
  }

  switch (v->type()) {
    case INT8:
      v->set_int8_value(static_cast<int8_t>(GetInt64()));
      return;
    case INT16:
      v->set_int16_value(static_cast<int16_t>(GetInt64()));
      return;
    case INT32:
      v->set_int32_value(static_cast<int32_t>(GetInt64()));
      return;
    case INT64:
      v->set_int64_value(static_cast<int64_t>(GetInt64()));
      return;
    case FLOAT:
      v->set_float_value(static_cast<float>(GetDouble()));
      return;
    case DOUBLE:
      v->set_double_value(GetDouble());
      return;
    case BOOL:
      v->set_bool_value((value_type() == ValueType::kTrue));
      return;
    case TIMESTAMP:
      v->set_timestamp_value(GetTimestamp());
      return;
    case STRING:
      v->set_string_value(GetString());
      return;
    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype " << v->type();
}

}  // namespace docdb
}  // namespace yb
