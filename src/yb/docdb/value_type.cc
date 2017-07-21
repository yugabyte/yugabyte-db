// Copyright (c) YugaByte, Inc.

#include "yb/docdb/value_type.h"

#include <glog/logging.h>

#include "yb/gutil/macros.h"
#include "yb/gutil/stringprintf.h"

namespace yb {
namespace docdb {

string ValueTypeToStr(ValueType value_type) {
  switch (value_type) {
    case ValueType::kGroupEnd: return "GroupEnd";
    case ValueType::kNull: return "Null";
    case ValueType::kFalse: return "False";
    case ValueType::kTrue: return "True";
    case ValueType::kStringDescending: return "StingDescending";
    case ValueType::kString: return "String";
    case ValueType::kInt64Descending: return "Int64Descending";
    case ValueType::kInt32Descending: return "Int32Descending";
    case ValueType::kInt64: return "Int64";
    case ValueType::kInt32: return "Int32";
    case ValueType::kDouble: return "Double";
    case ValueType::kFloat: return "Float";
    case ValueType::kDecimalDescending: return "DecimalDescending";
    case ValueType::kDecimal: return "Decimal";
    case ValueType::kTimestampDescending: return "TimestampDescending";
    case ValueType::kTimestamp: return "Timestamp";
    case ValueType::kInetaddressDescending: return "InetAddressDescending";
    case ValueType::kInetaddress: return "InetAddress";
    case ValueType::kUuidDescending: return "UuidDecending";
    case ValueType::kUuid: return "Uuid";
    case ValueType::kHybridTime: return "HybridTime";
    case ValueType::kUInt16Hash: return "UInt16Hash";
    case ValueType::kObject: return "Object";
    case ValueType::kRedisSet: return "RedisSet";
    case ValueType::kArray: return "Array";
    case ValueType::kArrayIndex: return "ArrayIndex";
    case ValueType::kTombstone: return "Tombstone";
    case ValueType ::kTtl: return "Ttl";
    case ValueType ::kColumnId: return "ColumnId";
    case ValueType ::kSystemColumnId: return "SystemColumnId";
    case ValueType::kInvalidValueType: return "InvalidValueType";
    // No default case so that we get a compiler warning (which we treat as an error) if we miss
    // a valid enum value here.
  }
  return StringPrintf("ValueType(0x%02x)", static_cast<uint8_t>(value_type));
}

}  // namespace docdb
}  // namespace yb
