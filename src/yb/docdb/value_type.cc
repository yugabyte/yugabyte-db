// Copyright (c) YugaByte, Inc.

#include "yb/docdb/value_type.h"

#include <glog/logging.h>

#include "yb/gutil/stringprintf.h"

namespace yb {
namespace docdb {

string ValueTypeToStr(ValueType value_type) {
  switch (value_type) {
    case ValueType::kGroupEnd: return "GroupEnd";
    case ValueType::kNull: return "Null";
    case ValueType::kFalse: return "False";
    case ValueType::kTrue: return "True";
    case ValueType::kString: return "String";
    case ValueType::kInt64: return "Int64";
    case ValueType::kDouble: return "Double";
    case ValueType::kTimestamp: return "Timestamp";
    case ValueType::kHybridTime: return "HybridTime";
    case ValueType::kUInt16Hash: return "UInt16Hash";
    case ValueType::kObject: return "Object";
    case ValueType::kArray: return "Array";
    case ValueType::kArrayIndex: return "ArrayIndex";
    case ValueType::kTombstone: return "Tombstone";
    case ValueType ::kTtl: return "Ttl";
    case ValueType::kInvalidValueType: return "InvalidValueType";
    // No default case so that we get a compiler warning (which we treat as an error) if we miss
    // a valid enum value here.
  }
  return StringPrintf("ValueType(0x%02x)", static_cast<uint8_t>(value_type));
}

}  // namespace docdb
}  // namespace yb
