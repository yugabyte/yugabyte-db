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

// ValueType determines the first byte of PrimitiveValue encoding. There are also some special value
// types, such as kGroupEnd, that are used to indicate where one part of a key ends and the next
// part begins. This is used in RocksDB key and value encoding. Changes to this enum may invalidate
// persistent data.
enum class ValueType : char {
  // This ValueType is used as -infinity for scanning purposes only.
  kLowest = 0,

  // All intents are stored in the beginning of the keyspace to be able to read them without
  // polluting cache with other values. Later we'll put intents in a separate rocksdb.
  kIntentPrefix = 10,

  // We use ASCII code 20 in order to have it before all other value types which can occur in key,
  // so intents will be written in the same order as original keys for which intents are written.
  kIntentType = 20,

  // This indicates the end of the "hashed" or "range" group of components of the primary key. This
  // needs to sort before all other value types, so that a DocKey that has a prefix of the sequence
  // of components of another key sorts before the other key.
  // kGroupEnd is also used as the end marker for a frozen value.
  kGroupEnd = '!',  // ASCII code 33 -- we pick the lowest code graphic character.

  // HybridTime must be lower than all other primitive types (other than kGroupEnd) so that
  // SubDocKeys that have fewer subkeys within a document sort above those that have all the same
  // subkeys and more. In our MVCC document model layout the hybrid time always appears at the end
  // of the key.
  kHybridTime = '#',  // ASCII code 35 (34 is double quote, which would be a bit confusing here).

  // Primitive value types

  // Null must be lower than the other primitive types so that it compares as smaller than them.
  // It is used for frozen CQL user-defined types (which can contain null elements) on ASC columns.
  kNull = '$',  // ASCII code 36

  // Counter to check cardinality.
  kCounter = '%',  // ASCII code 37

  // Forward and reverse mappings for sorted sets.
  kSSForward = '&', // ASCII code 38
  kSSReverse = '\'', // ASCII code 39

  kRedisSet = '(', // ASCII code 40
  // This is the redis timeseries type.
  kRedisTS = '+', // ASCII code 43
  kRedisSortedSet = ',', // ASCII code 44
  kInetaddress = '-',  // ASCII code 45
  kInetaddressDescending = '.',  // ASCII code 46
  kJsonb = '2', // ASCII code 50
  kFrozen = '<', // ASCII code 60
  kFrozenDescending = '>', // ASCII code 62
  kArray = 'A',  // ASCII code 65.
  kVarInt = 'B', // ASCII code 66
  kFloat = 'C',  // ASCII code 67
  kDouble = 'D',  // ASCII code 68
  kDecimal = 'E',  // ASCII code 69
  kFalse = 'F',  // ASCII code 70
  kUInt16Hash = 'G',  // ASCII code 71
  kInt32 = 'H',  // ASCII code 72
  kInt64 = 'I',  // ASCII code 73
  kSystemColumnId = 'J',  // ASCII code 74
  kColumnId = 'K',  // ASCII code 75
  kDoubleDescending = 'L',  // ASCII code 76
  kFloatDescending = 'M', // ASCII code 77
  kString = 'S',  // ASCII code 83
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
  kInt32Descending = 'e',  // ASCII code 101
  kVarIntDescending = 'f',  // ASCII code 102

  // Timestamp value in microseconds
  kTimestamp = 's',  // ASCII code 115
  // TTL value in milliseconds, optionally present at the start of a value.
  kTtl = 't',  // ASCII code 116
  kUserTimestamp = 'u',  // ASCII code 117
  kTransactionId = 'x', // ASCII code 120

  kObject = '{',  // ASCII code 123

  // Null desc must be higher than the other descending primitive types so that it compares as
  // bigger than them.
  // It is used for frozen CQL user-defined types (which can contain null elements) on DESC columns.
  kNullDescending = '|', // ASCII code 124

  // This is only needed when used as the end marker for a frozen value on a DESC column.
  kGroupEndDescending = '}',  // ASCII code 125 -- we pick the highest value below kHighest.

  // This ValueType is used as +infinity for scanning purposes only.
  kHighest = '~', // ASCII code 126

  // This is used for sanity checking. TODO: rename to kInvalid since this is an enum class.
  kInvalidValueType = 127,

  // ValueType which lexicographically higher than any other byte and is not used for encoding
  // value type.
  kMaxByte = '\xff',
};

// "Intent types" are used for single-tablet operations and cross-shard transactions. For example,
// multiple write-only operations don't need to conflict. However, if one operation is a
// read-modify-write snapshot isolation operation, then a write-only operation cannot proceed in
// parallel with it. Conflicts between intent types are handled according to the conflict matrix at
// https://goo.gl/Wbc663.

// "Weak" intents are obtained for parent nodes of a node that is a transaction is working with.
// E.g. if we're writing "a.b.c", we'll obtain weak write intents on "a" and "a.b", but a strong
// write intent on "a.b.c".
constexpr int kWeakIntentFlag         = 0b000;

// "Strong" intents are obtained on the node that an operation is working with. See the example
// above.
constexpr int kStrongIntentFlag       = 0b001;

constexpr int kReadIntentFlag         = 0b000;
constexpr int kWriteIntentFlag        = 0b010;

constexpr int kSerializableIntentFlag = 0b000;
constexpr int kSnapshotIntentFlag     = 0b100;

constexpr int kStrongWriteIntentFlags = kStrongIntentFlag | kWriteIntentFlag;

YB_DEFINE_ENUM(IntentType,
    ((kStrongSnapshotWrite,     kStrongIntentFlag | kWriteIntentFlag | kSnapshotIntentFlag))
    ((kWeakSnapshotWrite,       kWeakIntentFlag   | kWriteIntentFlag | kSnapshotIntentFlag))
    ((kStrongSerializableWrite, kStrongIntentFlag | kWriteIntentFlag | kSerializableIntentFlag))
    ((kWeakSerializableWrite,   kWeakIntentFlag   | kWriteIntentFlag | kSerializableIntentFlag))
    ((kStrongSerializableRead,  kStrongIntentFlag | kReadIntentFlag  | kSerializableIntentFlag))
    ((kWeakSerializableRead,    kWeakIntentFlag   | kReadIntentFlag  | kSerializableIntentFlag))
);

inline bool IsStrongIntent(IntentType intent) {
  return (static_cast<int>(intent) & kStrongIntentFlag) != 0;
}

inline bool IsStrongWriteIntent(IntentType intent_type) {
  return (static_cast<int>(intent_type) & kStrongWriteIntentFlags) == kStrongWriteIntentFlags;
}

inline bool IsWeakIntent(IntentType intent) {
  return !IsStrongIntent(intent);
}

inline bool IsWriteIntent(IntentType intent) {
  return (static_cast<int>(intent) & kWriteIntentFlag) != 0;
}

inline bool IsReadIntent(IntentType intent) {
  return !IsWriteIntent(intent);
}

inline bool IsSnapshotIntent(IntentType intent) {
  return (static_cast<int>(intent) & kSnapshotIntentFlag) != 0;
}

inline bool IsSerializableIntent(IntentType intent) {
  return !IsSnapshotIntent(intent);
}

// All primitive value types fall into this range, but not all value types in this range are
// primitive (e.g. object and tombstone are not).

constexpr ValueType kMinPrimitiveValueType = ValueType::kNull;
constexpr ValueType kMaxPrimitiveValueType = ValueType::kNullDescending;

std::string ToString(ValueType value_type);

// kArray is handled slightly differently and hence we only have kObject, kRedisTS and kRedisSet.
constexpr inline bool IsObjectType(const ValueType value_type) {
  return value_type == ValueType::kRedisTS || value_type == ValueType::kObject ||
      value_type == ValueType::kRedisSet || value_type == ValueType::kRedisSortedSet ||
      value_type == ValueType::kSSForward || value_type == ValueType::kSSReverse;
}

constexpr inline bool IsCollectionType(const ValueType value_type) {
  return IsObjectType(value_type) || value_type == ValueType::kArray;
}

constexpr inline bool IsPrimitiveValueType(const ValueType value_type) {
  return kMinPrimitiveValueType <= value_type && value_type <= kMaxPrimitiveValueType &&
         !IsCollectionType(value_type) &&
         value_type != ValueType::kTombstone;
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
