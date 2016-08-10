// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_VALUE_TYPE_H_
#define YB_DOCDB_VALUE_TYPE_H_

#include <string>

#include "rocksdb/slice.h"

namespace yb {
namespace docdb {

// Changes to this enum may invalidate persistent data.
enum class ValueType : char {
  // This indicates the end of the "hashed" or "range" group of components of the primary key.
  kGroupEnd = 0,

  // Primitive value types
  kNull = 1,
  kFalse = 2,
  kTrue = 3,
  kString = 4,
  kInt64 = 5,
  kArrayIndex = 6,
  kDouble = 7,
  kTimestamp = 8,
  // We allow putting a 32-bit hash in front of the document key. This hash is computed based on
  // the "hashed" components of the document key that precede "range" components.
  kUInt32Hash = 9,

  // Leave some room for more primitive types to be added later.

  // Non-primitive value types
  kObject = 64,
  kArray = 65,
  kTombstone = 66,

  // This is used for sanity checking.
  kInvalidValueType = 127
};

constexpr ValueType kMinPrimitiveValueType = ValueType::kNull;
constexpr ValueType kMaxPrimitiveValueType = ValueType::kUInt32Hash;
std::string ValueTypeToStr(ValueType value_type);

constexpr inline bool IsPrimitiveValueType(const ValueType value_type) {
  return kMinPrimitiveValueType <= value_type && value_type <= kMaxPrimitiveValueType;
}

inline ValueType DecodeValueType(const rocksdb::Slice& value) {
  assert(!value.empty());
  return static_cast<ValueType>(value.data()[0]);
}

inline ValueType ConsumeValueType(rocksdb::Slice* slice) {
  assert(!slice->empty());
  return static_cast<ValueType>(slice->ConsumeByte());
}

inline std::string EncodeValueType(const ValueType value_type) {
  std::string s(1, static_cast<char>(value_type));
  return s;
}

}
}

#endif
