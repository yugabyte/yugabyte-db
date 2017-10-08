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

#ifndef YB_DOCDB_VALUE_TYPE_H_
#define YB_DOCDB_VALUE_TYPE_H_

#include <string>

#include <glog/logging.h>

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/slice.h"

namespace yb {
namespace docdb {

// Changes to this enum may invalidate persistent data.
enum class ValueType : char {
  // This ValueType is used as -infinity for scanning purposes only.
  kLowest = ' ', // ASCII code 32

  // This indicates the end of the "hashed" or "range" group of components of the primary key. This
  // needs to sort before all other value types, so that a DocKey that has a prefix of the sequence
  // of components of another key sorts before the other key.
  kGroupEnd = '!',  // ASCII code 33 -- we pick the lowest code graphic character.
  // Note that intents also start with a ! character.
  // All intents are stored in the beginning of the keyspace to be able to read them without
  // polluting cache with other values. Later we'll put intents in a separate rocksdb.
  kIntentPrefix = '"', // ASCII code 34

  // HybridTime must be lower than all other primitive types (other than kGroupEnd) so that
  // SubDocKeys that have fewer subkeys within a document sort above those that have all the same
  // subkeys and more. In our MVCC document model layout the hybrid time always appears at the end
  // of the key.
  kHybridTime = '#',  // ASCII code 35 (34 is double quote, which would be a bit confusing here).

  // Primitive value types
  kString = '$',  // ASCII code 36
  kInetaddress = '-',  // ASCII code 45
  kInetaddressDescending = '.',  // ASCII code 46
  kArray = 'A',  // ASCII code 65.
  kFloat = 'C',  // ASCII code 67
  kDouble = 'D',  // ASCII code 68
  kDecimal = 'E',  // ASCII code 69
  kFalse = 'F',  // ASCII code 70
  kUInt16Hash = 'G',  // ASCII code 71
  kInt32 = 'H',  // ASCII code 72
  kInt64 = 'I',  // ASCII code 73
  kSystemColumnId = 'J',  // ASCII code 74
  kColumnId = 'K',  // ASCII code 75
  kNull = 'N',  // ASCII code 78
  kTrue = 'T',  // ASCII code 84
  kTombstone = 'X',  // ASCII code 88
  kArrayIndex = '[',  // ASCII code 91.

  // We allow putting a 32-bit hash in front of the document key. This hash is computed based on
  // the "hashed" components of the document key that precede "range" components.

  kUuid = '_', // ASCII code 95
  kUuidDescending = '`', // ASCII code 96
  kStringDescending = 'a',  // ASCII code 97
  kInt64Descending = 'b',  // ASCII code 98
  kTimestampDescending = 'c',  // ASCII code 99
  kDecimalDescending = 'd',  // ASCII code 100
  kIntentType = 'i', // ASCII code 105
  kInt32Descending = 'e',  // ASCII code 101

  // Timestamp value in microseconds
  kTimestamp = 's',  // ASCII code 115
  // TTL value in milliseconds, optionally present at the start of a value.
  kTtl = 't',  // ASCII code 116
  kTransactionId = 'x', // ASCII code 120

  kObject = '{',  // ASCII code 123
  kRedisSet = '(', // ASCII code 40

  // This ValueType is used as +infinity for scanning purposes only.
  kHighest = '~', // ASCII code 126

  // This is used for sanity checking. TODO: rename to kInvalid since this is an enum class.
  kInvalidValueType = 127
};

constexpr int kWeakIntentFlag         = 0b000;
constexpr int kStrongIntentFlag       = 0b001;

constexpr int kReadIntentFlag         = 0b000;
constexpr int kWriteIntentFlag        = 0b010;

constexpr int kSerializableIntentFlag = 0b000;
constexpr int kSnapshotIntentFlag     = 0b100;

// The purpose of different types of intents is to ensure that they conflict
// in appropriate ways according to the conflict matrix.
YB_DEFINE_ENUM(IntentType,
    ((kStrongSnapshotWrite, kStrongIntentFlag | kWriteIntentFlag | kSnapshotIntentFlag))
    ((kWeakSnapshotWrite, kWeakIntentFlag | kWriteIntentFlag | kSnapshotIntentFlag))
    ((kStrongSerializableWrite, kStrongIntentFlag | kWriteIntentFlag | kSerializableIntentFlag))
    ((kWeakSerializableWrite, kWeakIntentFlag | kWriteIntentFlag | kSerializableIntentFlag))
    ((kStrongSerializableRead, kStrongIntentFlag | kReadIntentFlag | kSerializableIntentFlag))
    ((kWeakSerializableRead, kWeakIntentFlag | kReadIntentFlag | kSerializableIntentFlag))
);

inline bool StrongIntent(IntentType intent) {
  return (static_cast<int>(intent) & kStrongIntentFlag) != 0;
}

// All primitive value types fall into this range, but not all value types in this range are
// primitive (e.g. object and tombstone are not).

constexpr ValueType kMinPrimitiveValueType = ValueType::kString;
constexpr ValueType kMaxPrimitiveValueType = ValueType::kObject;

std::string ToString(ValueType value_type);

constexpr inline bool IsPrimitiveValueType(const ValueType value_type) {
  return kMinPrimitiveValueType <= value_type && value_type <= kMaxPrimitiveValueType &&
         value_type != ValueType::kObject &&
         value_type != ValueType::kArray &&
         value_type != ValueType::kTombstone &&
         value_type != ValueType::kRedisSet;
}

// Decode the first byte of the given slice as a ValueType.
inline ValueType DecodeValueType(const rocksdb::Slice& value) {
  return value.empty() ? ValueType::kInvalidValueType : static_cast<ValueType>(value.data()[0]);
}

// Decode the first byte of the given slice as a ValueType and consume it.
inline ValueType ConsumeValueType(rocksdb::Slice* slice) {
  return slice->empty() ? ValueType::kInvalidValueType
                        : static_cast<ValueType>(slice->consume_byte());
}

inline std::ostream& operator<<(std::ostream& out, const ValueType value_type) {
  return out << ToString(value_type);
}

inline ValueType DecodeValueType(char value_type_byte) {
  return static_cast<ValueType>(value_type_byte);
}

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_VALUE_TYPE_H_
