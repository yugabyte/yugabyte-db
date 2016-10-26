// Copyright (c) YugaByte, Inc.

#include "yb/docdb/primitive_value.h"

#include <string>

#include <glog/logging.h>

#include "yb/docdb/doc_kv_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/bytes_formatter.h"

using std::string;
using strings::Substitute;
using yb::util::FormatBytesAsStr;

// We're listing all non-primitive value types at the end of switch statement instead of using a
// default clause so that we can ensure that we're handling all possible primitive value types
// at compile time.
#define IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH \
    case ValueType::kArray: FALLTHROUGH_INTENDED; \
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED; \
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED; \
    case ValueType::kObject: FALLTHROUGH_INTENDED; \
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
    case ValueType::kArrayIndex:
      return Substitute("ArrayIndex($0)", int64_val_);
    case ValueType::kTimestamp:
      // TODO: print out timestamps in a human-readable way?
      return timestamp_val_.ToDebugString();
    case ValueType::kUInt32Hash:
      return Substitute("UInt32Hash($0)", uint32_val_);
    case ValueType::kObject:
      return "{}";
    case ValueType::kTombstone:
      return "DEL";
    case ValueType::kArray:
      return "[]";

    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType:
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

    case ValueType::kArrayIndex:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kTimestamp:
      key_bytes->AppendTimestamp(timestamp_val_);
      return;

    case ValueType::kUInt32Hash:
      key_bytes->AppendUInt32(uint32_val_);
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
      AppendBigEndianUInt64(timestamp_val_.value(), &result);
      return result;

    case ValueType::kUInt32Hash:
      // Hashes are not allowed in a value.
      break;

    case ValueType::kArray: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType:
      break;
  }

  LOG(FATAL) << __FUNCTION__ << " not implemented for value type " << ValueTypeToStr(type_);
}

Status PrimitiveValue::DecodeFromKey(rocksdb::Slice* slice) {
  if (slice->empty()) {
    return STATUS(Corruption,
        "Cannot decode a primitive value in the key encoding format from an empty slice");
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
        return STATUS(Corruption, Substitute("Not enough bytes to decode a 64-bit integer: $0",
            slice->size()));
      }
      int64_val_ = BigEndian::Load64(slice->data()) ^ kInt64SignBitFlipMask;
      slice->remove_prefix(sizeof(int64_t));
      type_ = value_type;
      return Status::OK();

    case ValueType::kUInt32Hash:
      if (slice->size() < sizeof(int32_t)) {
        return STATUS(Corruption, Substitute("Not enough bytes to decode a 32-bit hash: $0",
            slice->size()));
      }
      uint32_val_ = BigEndian::Load32(slice->data());
      slice->remove_prefix(sizeof(uint32_t));
      type_ = value_type;
      return Status::OK();

    case ValueType::kTimestamp:
      if (slice->size() < kBytesPerTimestamp) {
        return STATUS(Corruption,
            Substitute("Not enough bytes to decode a timestamp: $0, need $1",
                slice->size(), kBytesPerTimestamp));
      }
      slice->remove_prefix(kBytesPerTimestamp);
      type_ = value_type;
      int64_val_ = BigEndian::Load64(slice->data());
      return Status::OK();

    case ValueType::kDouble:
      // Doubles are not allowed in a key as of 07/15/2016.
      break;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  return STATUS(Corruption,
      Substitute("Cannot decode value type $0 from the key encoding format",
          ValueTypeToStr(value_type)));
}

Status PrimitiveValue::DecodeFromValue(const rocksdb::Slice& rocksdb_value) {
  rocksdb::Slice slice(rocksdb_value);
  if (slice.empty()) {
    return STATUS(Corruption, "Cannot decode a value from an empty slice");
  }
  auto value_type = ConsumeValueType(&slice);
  this->~PrimitiveValue();
  // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
  // due to inability to allocate memory.
  type_ = ValueType::kNull;

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

    case ValueType::kArray:
      return STATUS(IllegalState, "Arrays are currently not supported");

    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32Hash: FALLTHROUGH_INTENDED;
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp:
      return STATUS(Corruption,
          Substitute("$0 is not allowed in a RocksDB value", ValueTypeToStr(value_type)));
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

PrimitiveValue PrimitiveValue::UInt32Hash(uint32_t hash) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kUInt32Hash;
  primitive_value.uint32_val_ = hash;
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
    case ValueType::kUInt32Hash: return uint32_val_ == other.uint32_val_;
    case ValueType::kTimestamp: return timestamp_val_.CompareTo(other.timestamp_val_) == 0;
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
    case ValueType::kUInt32Hash:
      return GenericCompare(uint32_val_, other.uint32_val_);
    case ValueType::kTimestamp:
      // Timestamps are sorted in reverse order.
      return -GenericCompare(timestamp_val_.value(), other.timestamp_val_.value());
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
    case DataType::BOOL:
      // TODO(mbautin): check if this is the right way to interpret a bool value in Kudu.
      return PrimitiveValue(*slice.data() == 0 ? ValueType::kFalse: ValueType::kTrue);
    default:
      LOG(FATAL) << "Converting Kudu value of type " << data_type
                 << " to docdb PrimitiveValue is currently not supported";
    }
}

}  // namespace docdb
}  // namespace yb
