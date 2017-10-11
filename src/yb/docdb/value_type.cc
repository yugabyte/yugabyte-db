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

#include "yb/docdb/value_type.h"

#include <glog/logging.h>

#include "yb/gutil/macros.h"
#include "yb/gutil/stringprintf.h"

namespace yb {
namespace docdb {

string ToString(ValueType value_type) {
  switch (value_type) {
    case ValueType::kGroupEnd: return "GroupEnd";
    case ValueType::kGroupEndDescending: return "GroupEndDescending";
    case ValueType::kIntentPrefix: return "IntentPrefix";
    case ValueType::kNull: return "Null";
    case ValueType::kNullDescending: return "NullDescending";
    case ValueType::kFalse: return "False";
    case ValueType::kTrue: return "True";
    case ValueType::kStringDescending: return "StringDescending";
    case ValueType::kString: return "String";
    case ValueType::kInt64Descending: return "Int64Descending";
    case ValueType::kInt32Descending: return "Int32Descending";
    case ValueType::kInt64: return "Int64";
    case ValueType::kInt32: return "Int32";
    case ValueType::kDouble: return "Double";
    case ValueType::kDoubleDescending: return "DoubleDescending";
    case ValueType::kFloat: return "Float";
    case ValueType::kFloatDescending: return "FloatDescending";
    case ValueType::kFrozen: return "Frozen";
    case ValueType::kFrozenDescending: return "FrozenDescending";
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
    case ValueType::kTtl: return "Ttl";
    case ValueType::kTransactionId: return "TransactionId";
    case ValueType::kIntentType: return "IntentType";
    case ValueType::kColumnId: return "ColumnId";
    case ValueType::kSystemColumnId: return "SystemColumnId";
    case ValueType::kLowest: return "-Inf";
    case ValueType::kHighest: return "+Inf";
    case ValueType::kInvalidValueType: return "InvalidValueType";
    // No default case so that we get a compiler warning (which we treat as an error) if we miss
    // a valid enum value here.
  }
  return StringPrintf("ValueType(0x%02x)", static_cast<uint8_t>(value_type));
}

}  // namespace docdb
}  // namespace yb
