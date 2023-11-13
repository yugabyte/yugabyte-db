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

#include <memory>
#include <string>

#include "yb/util/logging.h"

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/common/value.messages.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/key_entry_value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/compare_util.h"
#include "yb/util/decimal.h"
#include "yb/util/fast_varint.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::string;
using strings::Substitute;
using yb::QLValuePB;
using yb::common::Jsonb;
using yb::util::Decimal;
using yb::util::VarInt;
using yb::FormatBytesAsStr;
using yb::util::CompareUsingLessThan;
using yb::util::FastDecodeSignedVarIntUnsafe;
using yb::util::kInt32SignBitFlipMask;
using yb::util::AppendBigEndianUInt64;
using yb::util::AppendBigEndianUInt32;
using yb::util::DecodeInt64FromKey;
using yb::util::DecodeFloatFromKey;
using yb::util::DecodeDoubleFromKey;

// We're listing all non-primitive value types at the end of switch statement instead of using a
// default clause so that we can ensure that we're handling all possible primitive value types
// at compile time.
#define IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH \
    case ValueEntryType::kArray: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kInvalid: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kJsonb: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kObject: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kPackedRow: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kRedisList: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kRedisSet: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kRedisSortedSet: FALLTHROUGH_INTENDED;  \
    case ValueEntryType::kRedisTS: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kRowLock: FALLTHROUGH_INTENDED; \
    case ValueEntryType::kTombstone: \
  break

#define SELECT_VALUE_TYPE(name, sort_order) SelectValueType( \
    KeyEntryType::BOOST_PP_CAT(k, name), \
    KeyEntryType::BOOST_PP_CAT(BOOST_PP_CAT(k, name), Descending), \
    sort_order)

#define POD_FACTORY_IMPL(cls, ucase, lcase) \
  cls cls::ucase(lcase v, SortOrder sort_order) { \
    cls result; \
    result.type_ = SELECT_VALUE_TYPE(ucase, sort_order); \
    result.BOOST_PP_CAT(lcase, _val_) = v; \
    return result; \
  }

#define POD_FACTORY(ucase, lcase) \
  PrimitiveValue PrimitiveValue::ucase(lcase v) { \
    PrimitiveValue result; \
    result.type_ = PrimitiveValue::Type::BOOST_PP_CAT(k, ucase); \
    result.BOOST_PP_CAT(lcase, _val_) = v; \
    return result; \
  } \
  POD_FACTORY_IMPL(KeyEntryValue, ucase, lcase) \

namespace yb {
namespace docdb {

namespace {

bool IsTrue(ValueEntryType type) {
  return type == ValueEntryType::kTrue;
}

bool IsTrue(KeyEntryType type) {
  return type == KeyEntryType::kTrue || type == KeyEntryType::kTrueDescending;
}

template <class ValueType>
ValueType SelectValueType(ValueType ascending, ValueType descending, SortOrder sort_order) {
  return sort_order == SortOrder::kAscending ? ascending : descending;
}

template <class T>
string RealToString(T val) {
  string s = std::to_string(val);
  // Remove trailing zeros.
  auto dot_pos = s.find('.');
  if (dot_pos != string::npos) {
    s.erase(std::max(dot_pos + 1, s.find_last_not_of('0')) + 1, string::npos);
  }
  if (s == "0.0" && val != 0.0) {
    // Use the exponential notation for small numbers that would otherwise look like a zero.
    return StringPrintf("%E", val);
  }
  return s;
}

ValueEntryType VirtualValueToValueEntryType(QLVirtualValuePB value) {
  switch (value) {
    case QLVirtualValuePB::LIMIT_MAX: FALLTHROUGH_INTENDED;
    case QLVirtualValuePB::LIMIT_MIN: FALLTHROUGH_INTENDED;
    case QLVirtualValuePB::COUNTER: FALLTHROUGH_INTENDED;
    case QLVirtualValuePB::SS_FORWARD: FALLTHROUGH_INTENDED;
    case QLVirtualValuePB::SS_REVERSE:
      break;
    case QLVirtualValuePB::TOMBSTONE:
      return ValueEntryType::kTombstone;
    case QLVirtualValuePB::NULL_LOW:
      return ValueEntryType::kNullLow;
    case QLVirtualValuePB::ARRAY:
      return ValueEntryType::kArray;
  }
  FATAL_INVALID_ENUM_VALUE(QLVirtualValuePB, value);
}

KeyEntryType VirtualValueToKeyEntryType(QLVirtualValuePB value) {
  switch (value) {
    case QLVirtualValuePB::LIMIT_MAX:
      return KeyEntryType::kHighest;
    case QLVirtualValuePB::LIMIT_MIN:
      return KeyEntryType::kLowest;
    case QLVirtualValuePB::COUNTER:
      return KeyEntryType::kCounter;
    case QLVirtualValuePB::SS_FORWARD:
      return KeyEntryType::kSSForward;
    case QLVirtualValuePB::SS_REVERSE:
      return KeyEntryType::kSSReverse;
    case QLVirtualValuePB::NULL_LOW:
      return KeyEntryType::kNullLow;
    case QLVirtualValuePB::ARRAY:
    case QLVirtualValuePB::TOMBSTONE:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(QLVirtualValuePB, value);
}

std::string VarIntToString(const std::string& str_val) {
  util::VarInt varint;
  auto status = varint.DecodeFromComparable(str_val);
  if (!status.ok()) {
    LOG(ERROR) << "Unable to decode varint: " << status.message().ToString();
    return "";
  }
  return varint.ToString();
}

std::string DecimalToString(const std::string& str_val) {
  util::Decimal decimal;
  auto status = decimal.DecodeFromComparable(str_val);
  if (!status.ok()) {
    LOG(ERROR) << "Unable to decode decimal";
    return "";
  }
  return decimal.ToString();
}

std::string FrozenToString(const FrozenContainer& frozen) {
  std::stringstream ss;
  bool first = true;
  ss << "<";
  for (const auto& pv : frozen) {
    if (first) {
      first = false;
    } else {
      ss << ",";
    }
    ss << pv.ToString();
  }
  ss << ">";
  return ss.str();
}

// In Postgres, character value cannot have embedded \0 byte. Both YCQL and Redis
// allow embedded \0 byte but neither has collation concept so kCollString becomes
// a synonym for kString. If the value is not empty and the first byte is \0, in
// Postgres it indicates this is a collation encoded string and we use kCollString:
// (1) in Postgres kCollString means a collation encoded string;
// (2) in both YCQL and Redis kCollString is a synonym for kString so it is also correct;
inline bool IsCollationEncodedString(const Slice& val) {
  return !val.empty() && val[0] == '\0';
}

} // anonymous namespace

const PrimitiveValue PrimitiveValue::kInvalid = PrimitiveValue(ValueEntryType::kInvalid);
const PrimitiveValue PrimitiveValue::kTombstone = PrimitiveValue(ValueEntryType::kTombstone);
const PrimitiveValue PrimitiveValue::kObject = PrimitiveValue(ValueEntryType::kObject);
const KeyEntryValue KeyEntryValue::kLivenessColumn = KeyEntryValue::SystemColumnId(
    SystemColumnIds::kLivenessColumn);

std::string PrimitiveValue::ToString() const {
  switch (type_) {
    case ValueEntryType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueEntryType::kNullLow:
      return "null";
    case ValueEntryType::kGinNull:
      switch (gin_null_val_) {
        // case 0, gin:norm-key, should not exist since the actual data would be used instead.
        case 1:
          return "GinNullKey";
        case 2:
          return "GinEmptyItem";
        case 3:
          return "GinNullItem";
        // case -1, gin:empty-query, should not exist since that's internal to postgres.
        default:
          LOG(FATAL) << "Unexpected gin null category: " << gin_null_val_;
      }
    case ValueEntryType::kFalse:
      return "false";
    case ValueEntryType::kTrue:
      return "true";
    case ValueEntryType::kInvalid:
      return "invalid";
    case ValueEntryType::kCollString: FALLTHROUGH_INTENDED;
    case ValueEntryType::kString:
      return FormatBytesAsStr(str_val_);
    case ValueEntryType::kInt32:
      return std::to_string(int32_val_);
    case ValueEntryType::kUInt32:
      return std::to_string(uint32_val_);
    case ValueEntryType::kUInt64:
      return std::to_string(uint64_val_);
    case ValueEntryType::kInt64:
      return std::to_string(int64_val_);
    case ValueEntryType::kFloat:
      return RealToString(float_val_);
    case ValueEntryType::kFrozen:
      return FrozenToString(*frozen_val_);
    case ValueEntryType::kDouble:
      return RealToString(double_val_);
    case ValueEntryType::kDecimal:
      return DecimalToString(str_val_);
    case ValueEntryType::kVarInt:
      return VarIntToString(str_val_);
    case ValueEntryType::kTimestamp:
      return timestamp_val_.ToString();
    case ValueEntryType::kInetaddress:
      return inetaddress_val_->ToString();
    case ValueEntryType::kJsonb:
      return FormatBytesAsStr(str_val_);
    case ValueEntryType::kUuid:
      return uuid_val_.ToString();
    case ValueEntryType::kRowLock:
      return "l";
    case ValueEntryType::kArrayIndex:
      return Substitute("ArrayIndex($0)", int64_val_);
    case ValueEntryType::kPackedRow:
      return "<PACKED ROW>";
    case ValueEntryType::kObject:
      return "{}";
    case ValueEntryType::kRedisSet:
      return "()";
    case ValueEntryType::kRedisTS:
      return "<>";
    case ValueEntryType::kRedisSortedSet:
      return "(->)";
    case ValueEntryType::kTombstone:
      return "DEL";
    case ValueEntryType::kRedisList: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArray:
      return "[]";
    case ValueEntryType::kTableId:
      return Format("TableId($0)", uuid_val_.ToString());
    case ValueEntryType::kColocationId:
      return Format("ColocationId($0)", uint32_val_);
    case ValueEntryType::kTransactionId:
      return Substitute("TransactionId($0)", uuid_val_.ToString());
    case ValueEntryType::kSubTransactionId:
      return Substitute("SubTransactionId($0)", uint32_val_);
    case ValueEntryType::kWriteId:
      return Format("WriteId($0)", int32_val_);
    case ValueEntryType::kMaxByte:
      return "0xff";
  }
  FATAL_INVALID_ENUM_VALUE(ValueEntryType, type_);
}

// Values that contains only KeyEntryType, w/o any additional data.
#define CASE_EMPTY_KEY_ENTRY_TYPES \
    case KeyEntryType::kCounter: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kFalse: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kFalseDescending: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kHighest: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kLowest: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kNullHigh: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kNullLow: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kSSForward: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kSSReverse: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kTrue: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kTrueDescending:

#define IGNORE_SPECIAL_KEY_ENTRY_TYPES \
    case KeyEntryType::kBitSet: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kExternalIntents: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kGreaterThanIntentType: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kGroupEnd: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kGroupEndDescending: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kInvalid: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kMaxByte: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kMergeFlags: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kObsoleteIntentPrefix: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kTtl: FALLTHROUGH_INTENDED; \
    case KeyEntryType::kUserTimestamp: \
      break

void KeyEntryValue::AppendToKey(KeyBytes* key_bytes) const {
  key_bytes->AppendKeyEntryType(type_);
  switch (type_) {
    CASE_EMPTY_KEY_ENTRY_TYPES
      return;

    case KeyEntryType::kCollString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kString:
      key_bytes->AppendString(str_val_);
      return;

    case KeyEntryType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kStringDescending:
      key_bytes->AppendDescendingString(str_val_);
      return;

    case KeyEntryType::kInt64:
      key_bytes->AppendInt64(int64_val_);
      return;

    case KeyEntryType::kInt32:
      key_bytes->AppendInt32(int32_val_);
      return;

    case KeyEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32:
      key_bytes->AppendUInt32(uint32_val_);
      return;

    case KeyEntryType::kUInt32Descending:
      key_bytes->AppendDescendingUInt32(uint32_val_);
      return;

    case KeyEntryType::kInt32Descending:
      key_bytes->AppendDescendingInt32(int32_val_);
      return;

    case KeyEntryType::kInt64Descending:
      key_bytes->AppendDescendingInt64(int64_val_);
      return;

    case KeyEntryType::kUInt64:
      key_bytes->AppendUInt64(uint64_val_);
      return;

    case KeyEntryType::kUInt64Descending:
      key_bytes->AppendDescendingUInt64(uint64_val_);
      return;

    case KeyEntryType::kDouble:
      key_bytes->AppendDouble(double_val_);
      return;

    case KeyEntryType::kDoubleDescending:
      key_bytes->AppendDescendingDouble(double_val_);
      return;

    case KeyEntryType::kFloat:
      key_bytes->AppendFloat(float_val_);
      return;

    case KeyEntryType::kFloatDescending:
      key_bytes->AppendDescendingFloat(float_val_);
      return;

    case KeyEntryType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFrozen: {
      for (const auto& pv : *frozen_val_) {
        pv.AppendToKey(key_bytes);
      }
      key_bytes->AppendKeyEntryType(type_ == KeyEntryType::kFrozenDescending
          ? KeyEntryType::kGroupEndDescending : KeyEntryType::kGroupEnd);
      return;
    }

    case KeyEntryType::kDecimal:
      key_bytes->AppendDecimal(str_val_);
      return;

    case KeyEntryType::kDecimalDescending:
      key_bytes->AppendDecimalDescending(str_val_);
      return;

    case KeyEntryType::kVarInt:
      key_bytes->AppendVarInt(str_val_);
      return;

    case KeyEntryType::kVarIntDescending:
      key_bytes->AppendVarIntDescending(str_val_);
      return;

    case KeyEntryType::kTimestamp:
      key_bytes->AppendInt64(timestamp_val_.ToInt64());
      return;

    case KeyEntryType::kTimestampDescending:
      key_bytes->AppendDescendingInt64(timestamp_val_.ToInt64());
      return;

    case KeyEntryType::kInetaddress: {
      key_bytes->AppendString(inetaddress_val_->ToBytes());
      return;
    }

    case KeyEntryType::kInetaddressDescending: {
      key_bytes->AppendDescendingString(inetaddress_val_->ToBytes());
      return;
    }

    case KeyEntryType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case KeyEntryType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTableId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUuid: {
      std::string bytes;
      uuid_val_.EncodeToComparable(&bytes);
      key_bytes->AppendString(bytes);
      return;
    }

    case KeyEntryType::kUuidDescending: {
      std::string bytes;
      uuid_val_.EncodeToComparable(&bytes);
      key_bytes->AppendDescendingString(bytes);
      return;
    }

    case KeyEntryType::kArrayIndex:
      key_bytes->AppendInt64(int64_val_);
      return;

    case KeyEntryType::kHybridTime:
      hybrid_time_val_.AppendEncodedInDocDbFormat(key_bytes->mutable_data());
      return;

    case KeyEntryType::kUInt16Hash:
      key_bytes->AppendUInt16(uint16_val_);
      return;

    case KeyEntryType::kColumnId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSystemColumnId:
      key_bytes->AppendColumnId(column_id_val_);
      return;

    case KeyEntryType::kObsoleteIntentType:
      key_bytes->AppendIntentTypeSet(ObsoleteIntentTypeToSet(uint16_val_));
      return;

    case KeyEntryType::kObsoleteIntentTypeSet:
      key_bytes->AppendIntentTypeSet(ObsoleteIntentTypeSetToNew(uint16_val_));
      return;

    case KeyEntryType::kIntentTypeSet:
      key_bytes->AppendIntentTypeSet(IntentTypeSet(uint16_val_));
      return;

    case KeyEntryType::kGinNull:
      key_bytes->AppendUint8(gin_null_val_);
      return;

    IGNORE_SPECIAL_KEY_ENTRY_TYPES;
  }
  FATAL_INVALID_ENUM_VALUE(KeyEntryType, type_);
}

size_t KeyEntryValue::GetEncodedKeyEntryValueSize(const DataType& data_type) {
  constexpr size_t key_entry_type_size = 1;
  switch (data_type) {
    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case BOOL:
      return key_entry_type_size;
    case INT8: FALLTHROUGH_INTENDED;
    case INT16: FALLTHROUGH_INTENDED;
    case INT32: FALLTHROUGH_INTENDED;
    case FLOAT:
      return key_entry_type_size + sizeof(int32_t);

    case UINT32: FALLTHROUGH_INTENDED;
    case DATE:
      return key_entry_type_size + sizeof(uint32_t);

    case INT64: FALLTHROUGH_INTENDED;
    case DOUBLE: FALLTHROUGH_INTENDED;
    case TIME:
      return key_entry_type_size + sizeof(int64_t);

    case UINT64:
      return key_entry_type_size + sizeof(uint64_t);

    case TIMESTAMP:
      return key_entry_type_size + sizeof(Timestamp);

    case UUID: FALLTHROUGH_INTENDED;
    case TIMEUUID: FALLTHROUGH_INTENDED;
    case STRING: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case DECIMAL: FALLTHROUGH_INTENDED;
    case VARINT: FALLTHROUGH_INTENDED;
    case INET: FALLTHROUGH_INTENDED;
    case LIST: FALLTHROUGH_INTENDED;
    case MAP: FALLTHROUGH_INTENDED;
    case SET: FALLTHROUGH_INTENDED;
    case TUPLE: FALLTHROUGH_INTENDED;
    case TYPEARGS: FALLTHROUGH_INTENDED;
    case USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case FROZEN: FALLTHROUGH_INTENDED;
    case JSONB: FALLTHROUGH_INTENDED;
    case UINT8: FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case GIN_NULL: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      return 0;
  }

  LOG(FATAL) << "GetEncodedKeyEntryValueSize: unsupported ql_type: "
             << QLType::ToCQLString(data_type);
}

namespace {

template <class Buffer>
void AddValueType(
    ValueEntryType ascending, ValueEntryType descending, SortingType sorting_type, Buffer* out) {
  if (sorting_type == SortingType::kDescending ||
      sorting_type == SortingType::kDescendingNullsLast) {
    out->push_back(static_cast<char>(descending));
  } else {
    out->push_back(static_cast<char>(ascending));
  }
}

// Flags for jsonb.
// Indicates that the stored jsonb is the complete jsonb value and not a partial update to jsonb.
static constexpr int64_t kCompleteJsonb = 1;

class SizeCounter {
 public:
  size_t value() const {
    return value_;
  }

  void push_back(char ch) {
    ++value_;
  }

  void append(const std::string& str) {
    value_ += str.size();
  }

  void append(const char* str, size_t size) {
    value_ += size;
  }

 private:
  size_t value_ = 0;
};

template <class Buffer>
void DoAppendEncodedValue(const QLValuePB& value, Buffer* out) {
  switch (value.value_case()) {
    case QLValuePB::kInt8Value:
      out->push_back(ValueEntryTypeAsChar::kInt32);
      AppendBigEndianUInt32(value.int8_value(), out);
      return;
    case QLValuePB::kInt16Value:
      out->push_back(ValueEntryTypeAsChar::kInt32);
      AppendBigEndianUInt32(value.int16_value(), out);
      return;
    case QLValuePB::kInt32Value:
      out->push_back(ValueEntryTypeAsChar::kInt32);
      AppendBigEndianUInt32(value.int32_value(), out);
      return;
    case QLValuePB::kInt64Value:
      out->push_back(ValueEntryTypeAsChar::kInt64);
      AppendBigEndianUInt64(value.int64_value(), out);
      return;
    case QLValuePB::kUint32Value:
      out->push_back(ValueEntryTypeAsChar::kUInt32);
      AppendBigEndianUInt32(value.uint32_value(), out);
      return;
    case QLValuePB::kUint64Value:
      out->push_back(ValueEntryTypeAsChar::kUInt64);
      AppendBigEndianUInt64(value.uint64_value(), out);
      return;
    case QLValuePB::kFloatValue:
      out->push_back(ValueEntryTypeAsChar::kFloat);
      AppendBigEndianUInt32(bit_cast<uint32_t>(util::CanonicalizeFloat(value.float_value())), out);
      return;
    case QLValuePB::kDoubleValue:
      out->push_back(ValueEntryTypeAsChar::kDouble);
      AppendBigEndianUInt64(
          bit_cast<uint64_t>(util::CanonicalizeDouble(value.double_value())), out);
      return;
    case QLValuePB::kDecimalValue:
      out->push_back(ValueEntryTypeAsChar::kDecimal);
      out->append(value.decimal_value());
      return;
    case QLValuePB::kVarintValue:
      out->push_back(ValueEntryTypeAsChar::kVarInt);
      out->append(value.varint_value());
      return;
    case QLValuePB::kStringValue: {
      const auto& val = value.string_value();
      out->push_back(IsCollationEncodedString(val) ? ValueEntryTypeAsChar::kCollString
                                                   : ValueEntryTypeAsChar::kString);
      out->append(val);
      return;
    }
    case QLValuePB::kBinaryValue:
      out->push_back(ValueEntryTypeAsChar::kString);
      out->append(value.binary_value());
      return;
    case QLValuePB::kBoolValue:
      if (value.bool_value()) {
        out->push_back(ValueEntryTypeAsChar::kTrue);
      } else {
        out->push_back(ValueEntryTypeAsChar::kFalse);
      }
      return;
    case QLValuePB::kTimestampValue:
      out->push_back(ValueEntryTypeAsChar::kTimestamp);
      AppendBigEndianUInt64(QLValue::timestamp_value(value).ToInt64(), out);
      return;
    case QLValuePB::kDateValue:
      out->push_back(ValueEntryTypeAsChar::kUInt32);
      AppendBigEndianUInt32(value.date_value(), out);
      return;
    case QLValuePB::kTimeValue:
      out->push_back(ValueEntryTypeAsChar::kInt64);
      AppendBigEndianUInt64(value.time_value(), out);
      return;
    case QLValuePB::kInetaddressValue:
      out->push_back(ValueEntryTypeAsChar::kInetaddress);
      QLValue::inetaddress_value(value).AppendToBytes(out);
      return;
    case QLValuePB::kJsonbValue:
      out->push_back(ValueEntryTypeAsChar::kJsonb);
      // Append the jsonb flags.
      AppendBigEndianUInt64(kCompleteJsonb, out);
      // Append the jsonb serialized blob.
      out->append(QLValue::jsonb_value(value));
      return;
    case QLValuePB::kUuidValue:
      out->push_back(ValueEntryTypeAsChar::kUuid);
      QLValue::uuid_value(value).AppendEncodedComparable(out);
      return;
    case QLValuePB::kTimeuuidValue:
      out->push_back(ValueEntryTypeAsChar::kUuid);
      QLValue::timeuuid_value(value).AppendEncodedComparable(out);
      return;
    case QLValuePB::kFrozenValue: {
      const QLSeqValuePB& frozen = value.frozen_value();
      out->push_back(ValueEntryTypeAsChar::kFrozen);
      auto null_value_type = KeyEntryType::kNullLow;
      KeyBytes key;
      for (int i = 0; i < frozen.elems_size(); i++) {
        if (IsNull(frozen.elems(i))) {
          key.AppendKeyEntryType(null_value_type);
        } else {
          KeyEntryValue::FromQLValuePB(frozen.elems(i), SortingType::kNotSpecified).AppendToKey(
              &key);
        }
      }
      key.AppendKeyEntryType(KeyEntryType::kGroupEnd);
      out->append(key.data().AsSlice().cdata(), key.data().size());
      return;
    }
    case QLValuePB::VALUE_NOT_SET:
      out->push_back(ValueEntryTypeAsChar::kTombstone);
      return;

    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kTupleValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      break;

    case QLValuePB::kVirtualValue:
      out->push_back(static_cast<char>(VirtualValueToValueEntryType(value.virtual_value())));
      return;
    case QLValuePB::kGinNullValue:
      out->push_back(ValueEntryTypeAsChar::kGinNull);
      out->push_back(static_cast<char>(value.gin_null_value()));
      return;
    // default: fall through
  }

  FATAL_INVALID_ENUM_VALUE(QLValuePB::ValueCase, value.value_case());
}

} // namespace

void AppendEncodedValue(const QLValuePB& value, ValueBuffer* out) {
  DoAppendEncodedValue(value, out);
}

void AppendEncodedValue(const QLValuePB& value, std::string* out) {
  DoAppendEncodedValue(value, out);
}

size_t EncodedValueSize(const QLValuePB& value) {
  SizeCounter counter;
  DoAppendEncodedValue(value, &counter);
  return counter.value();
}

Status KeyEntryValue::DecodeFromKey(Slice* slice) {
  return DecodeKey(slice, this);
}

Result<KeyEntryValue> KeyEntryValue::FullyDecodeFromKey(const Slice& slice) {
  auto slice_copy = slice;
  KeyEntryValue result;
  RETURN_NOT_OK(result.DecodeFromKey(&slice_copy));
  if (!slice_copy.empty()) {
    return STATUS_FORMAT(
        Corruption,
        "Extra data after decoding key entry value: $0 - $1",
        slice.WithoutSuffix(slice_copy.size()).ToDebugHexString(),
        slice_copy.ToDebugHexString());
  }
  return result;
}

Status KeyEntryValue::DecodeKey(Slice* slice, KeyEntryValue* out) {
  // A copy for error reporting.
  const auto input_slice = *slice;

  if (slice->empty()) {
    return STATUS_SUBSTITUTE(Corruption,
        "Cannot decode a primitive value in the key encoding format from an empty slice: $0",
        ToShortDebugStr(input_slice));
  }
  auto type = ConsumeKeyEntryType(slice);
  KeyEntryType dummy_type;
  KeyEntryType& type_ref = out ? out->type_ : dummy_type;

  if (out) {
    out->~KeyEntryValue();
    // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
    // due to inability to allocate memory.
  }
  type_ref = KeyEntryType::kNullLow;

  switch (type) {
    CASE_EMPTY_KEY_ENTRY_TYPES
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kCollStringDescending:  FALLTHROUGH_INTENDED;
    case KeyEntryType::kStringDescending: {
      if (out) {
        string result;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &result));
        new (&out->str_val_) string(std::move(result));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      // Only set type to string after string field initialization succeeds.
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kCollString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kString: {
      if (out) {
        string result;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &result));
        new (&out->str_val_) string(std::move(result));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      // Only set type to string after string field initialization succeeds.
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kFrozenDescending:
    case KeyEntryType::kFrozen: {
      const auto end_marker_value_type = (type == KeyEntryType::kFrozenDescending)
          ? KeyEntryType::kGroupEndDescending
          : KeyEntryType::kGroupEnd;

      std::unique_ptr<FrozenContainer> frozen_val(out ? new FrozenContainer() : nullptr);
      while (!slice->empty()) {
        auto current_value_type = DecodeKeyEntryType(*slice->data());
        if (current_value_type == end_marker_value_type) {
          slice->consume_byte();
          type_ref = type;
          if (frozen_val) {
            out->frozen_val_ = frozen_val.release();
          }
          return Status::OK();
        } else {
          if (frozen_val) {
            frozen_val->emplace_back();
            RETURN_NOT_OK(DecodeKey(slice, &frozen_val->back()));
          } else {
            RETURN_NOT_OK(DecodeKey(slice, nullptr));
          }
        }
      }

      return STATUS(Corruption, "Reached end of slice looking for frozen group end marker");
    }

    case KeyEntryType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimal: {
      util::Decimal decimal;
      Slice slice_temp(slice->data(), slice->size());
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice_temp, &num_decoded_bytes));
      if (type == KeyEntryType::kDecimalDescending) {
        // When we encode a descending decimal, we do a bitwise negation of each byte, which changes
        // the sign of the number. This way we reverse the sorting order. decimal.Negate() restores
        // the original sign of the number.
        decimal.Negate();
      }
      if (out) { // TODO avoid using temp variable, when out is nullptr
        new(&out->str_val_) string(decimal.EncodeToComparable());
      }
      slice->remove_prefix(num_decoded_bytes);
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kVarInt: {
      util::VarInt varint;
      Slice slice_temp(slice->data(), slice->size());
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(varint.DecodeFromComparable(slice_temp, &num_decoded_bytes));
      if (type == KeyEntryType::kVarIntDescending) {
        varint.Negate();
      }
      if (out) { // TODO avoid using temp variable, when out is nullptr
        new(&out->str_val_) string(varint.EncodeToComparable());
      }
      slice->remove_prefix(num_decoded_bytes);
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kGinNull: {
      if (slice->size() < sizeof(uint8_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode an 8-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->gin_null_val_ = slice->data()[0];
      }
      slice->remove_prefix(sizeof(uint8_t));
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt32:
      if (slice->size() < sizeof(int32_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 32-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->int32_val_ = BigEndian::Load32(slice->data()) ^ kInt32SignBitFlipMask;
        if (type == KeyEntryType::kInt32Descending) {
          out->int32_val_ = ~out->int32_val_;
        }
      }
      slice->remove_prefix(sizeof(int32_t));
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32:
      if (slice->size() < sizeof(uint32_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 32-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->uint32_val_ = BigEndian::Load32(slice->data());
        if (type == KeyEntryType::kUInt32Descending) {
          out->uint32_val_ = ~out->uint32_val_;
        }
      }
      slice->remove_prefix(sizeof(uint32_t));
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kUInt64Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt64:
      if (slice->size() < sizeof(uint64_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 64-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->uint64_val_ = BigEndian::Load64(slice->data());
        if (type == KeyEntryType::kUInt64Descending) {
          out->uint64_val_ = ~out->uint64_val_;
        }
      }
      slice->remove_prefix(sizeof(uint64_t));
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kInt64Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt64: FALLTHROUGH_INTENDED;
    case KeyEntryType::kArrayIndex:
      if (slice->size() < sizeof(int64_t)) {
        return STATUS_SUBSTITUTE(Corruption,
            "Not enough bytes to decode a 64-bit integer: $0",
            slice->size());
      }
      if (out) {
        out->int64_val_ = DecodeInt64FromKey(*slice);
        if (type == KeyEntryType::kInt64Descending) {
          out->int64_val_ = ~out->int64_val_;
        }
      }
      slice->remove_prefix(sizeof(int64_t));
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kUInt16Hash:
      if (slice->size() < sizeof(uint16_t)) {
        return STATUS(Corruption, Substitute("Not enough bytes to decode a 16-bit hash: $0",
                                             slice->size()));
      }
      if (out) {
        out->uint16_val_ = BigEndian::Load16(slice->data());
      }
      slice->remove_prefix(sizeof(uint16_t));
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTimestamp: {
      if (slice->size() < sizeof(Timestamp)) {
        return STATUS(Corruption,
            Substitute("Not enough bytes to decode a Timestamp: $0, need $1",
                slice->size(), sizeof(Timestamp)));
      }
      if (out) {
        const auto uint64_timestamp = DecodeInt64FromKey(*slice);
        if (type == KeyEntryType::kTimestampDescending) {
          // Flip all the bits after loading the integer.
          out->timestamp_val_ = Timestamp(~uint64_timestamp);
        } else {
          out->timestamp_val_ = Timestamp(uint64_timestamp);
        }
      }
      slice->remove_prefix(sizeof(Timestamp));
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kInetaddress:
    case KeyEntryType::kInetaddressDescending: {
      auto decoder = &DecodeComplementZeroEncodedStr;
      if (type == KeyEntryType::kInetaddress) {
        decoder = &DecodeZeroEncodedStr;
      }
      std::unique_ptr<InetAddress> inetaddress_val(out ? new InetAddress() : nullptr);
      if (inetaddress_val) {
        std::string bytes;
        RETURN_NOT_OK(decoder(slice, &bytes));
        RETURN_NOT_OK(inetaddress_val->FromSlice(bytes));
      } else {
        RETURN_NOT_OK(decoder(slice, nullptr));
      }
      type_ref = type;
      if (inetaddress_val) {
        out->inetaddress_val_ = inetaddress_val.release();
      }
      return Status::OK();
    }

    case KeyEntryType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case KeyEntryType::kExternalTransactionId:
      if (slice->size() < boost::uuids::uuid::static_size()) {
        return STATUS_FORMAT(Corruption, "Not enough bytes for UUID: $0", slice->size());
      }
      if (out) {
        out->uuid_val_ = VERIFY_RESULT(Uuid::FromSlice(
            slice->Prefix(boost::uuids::uuid::static_size())));
      }
      slice->remove_prefix(boost::uuids::uuid::static_size());
      type_ref = type;
      return Status::OK();

    case KeyEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTableId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUuid: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &bytes));
        new(&out->uuid_val_) Uuid(VERIFY_RESULT(Uuid::FromComparable(bytes)));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kUuidDescending: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &bytes));
        new(&out->uuid_val_) Uuid(VERIFY_RESULT(Uuid::FromComparable(bytes)));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kColumnId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSystemColumnId: {
      // Decode varint
      {
        ColumnId dummy_column_id;
        ColumnId& column_id_ref = out ? out->column_id_val_ : dummy_column_id;
        int64_t column_id_as_int64 = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(slice));
        RETURN_NOT_OK(ColumnId::FromInt64(column_id_as_int64, &column_id_ref));
      }

      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kHybridTime: {
      if (out) {
        new (&out->hybrid_time_val_) DocHybridTime(VERIFY_RESULT(DocHybridTime::DecodeFrom(slice)));
      } else {
        RETURN_NOT_OK(DocHybridTime::DecodeFrom(slice));
      }

      type_ref = KeyEntryType::kHybridTime;
      return Status::OK();
    }

    case KeyEntryType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentType: {
      if (slice->empty()) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a TypeSet");
      }
      uint16_t value = static_cast<uint16_t>(slice->consume_byte());
      if (out) {
        out->uint16_val_ = value;
      }
      type_ref = type;
      return Status::OK();
    }

    case KeyEntryType::kFloatDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFloat: {
      if (slice->size() < sizeof(float_t)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a float: $0", slice->size());
      }
      if (out) {
        if (type == KeyEntryType::kFloatDescending) {
          out->float_val_ = DecodeFloatFromKey(*slice, /* descending */ true);
        } else {
          out->float_val_ = DecodeFloatFromKey(*slice);
        }
      }
      slice->remove_prefix(sizeof(float_t));
      type_ref = type;
      return Status::OK();
    }
    case KeyEntryType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDouble: {
      if (slice->size() < sizeof(double_t)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a float: $0", slice->size());
      }
      if (out) {
        if (type == KeyEntryType::kDoubleDescending) {
          out->double_val_ = DecodeDoubleFromKey(*slice, /* descending */ true);
        } else {
          out->double_val_ = DecodeDoubleFromKey(*slice);
        }
      }
      slice->remove_prefix(sizeof(double_t));
      type_ref = type;
      return Status::OK();
    }

    IGNORE_SPECIAL_KEY_ENTRY_TYPES;
  }
  return STATUS_FORMAT(
      Corruption,
      "Cannot decode value type $0 from the key encoding format: $1",
      type,
      ToShortDebugStr(input_slice));
}

Status PrimitiveValue::DecodeFromValue(const Slice& rocksdb_slice) {
  RSTATUS_DCHECK(!rocksdb_slice.empty(), Corruption, "Cannot decode a value from an empty slice");
  Slice slice(rocksdb_slice);
  this->~PrimitiveValue();
  // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
  // due to inability to allocate memory.
  type_ = ValueEntryType::kNullLow;

  const auto value_type = ConsumeValueEntryType(&slice);

  // TODO: ensure we consume all data from the given slice.
  switch (value_type) {
    case ValueEntryType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueEntryType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueEntryType::kFalse: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTrue: FALLTHROUGH_INTENDED;
    case ValueEntryType::kObject: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArray: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisList: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisTS: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisSortedSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTombstone:
      type_ = value_type;
      complex_data_structure_ = nullptr;
      return Status::OK();

    case ValueEntryType::kFrozen: {
      auto end_marker_value_type = KeyEntryType::kGroupEnd;

      auto frozen_val = std::make_unique<FrozenContainer>();
      while (!slice.empty()) {
        auto current_value_type = static_cast<KeyEntryType>(*slice.data());
        if (current_value_type == end_marker_value_type) {
          slice.consume_byte();
          type_ = value_type;
          frozen_val_ = frozen_val.release();
          return Status::OK();
        } else {
          // Frozen elems are encoded as keys even in values.
          frozen_val->emplace_back();
          RETURN_NOT_OK(frozen_val->back().DecodeFromKey(&slice));
        }
      }

      return STATUS(Corruption, "Reached end of slice looking for frozen group end marker");
    }
    case ValueEntryType::kCollString: FALLTHROUGH_INTENDED;
    case ValueEntryType::kString:
      new(&str_val_) string(slice.cdata(), slice.size());
      // Only set type to string after string field initialization succeeds.
      type_ = value_type;
      return Status::OK();

    case ValueEntryType::kGinNull:
      if (slice.size() != sizeof(uint8_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      gin_null_val_ = slice.data()[0];
      return Status::OK();

    case ValueEntryType::kInt32: FALLTHROUGH_INTENDED;
    case ValueEntryType::kWriteId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kFloat:
      if (slice.size() != sizeof(int32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      int32_val_ = BigEndian::Load32(slice.data());
      return Status::OK();

    case ValueEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUInt32: FALLTHROUGH_INTENDED;
    case ValueEntryType::kSubTransactionId:
      if (slice.size() != sizeof(uint32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      uint32_val_ = BigEndian::Load32(slice.data());
      return Status::OK();

    case ValueEntryType::kUInt64:
      if (slice.size() != sizeof(uint64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      uint64_val_ = BigEndian::Load64(slice.data());
      return Status::OK();

    case ValueEntryType::kInt64: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArrayIndex: FALLTHROUGH_INTENDED;
    case ValueEntryType::kDouble:
      if (slice.size() != sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      int64_val_ = BigEndian::Load64(slice.data());
      return Status::OK();

    case ValueEntryType::kDecimal: {
      util::Decimal decimal;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      type_ = value_type;
      new(&str_val_) string(decimal.EncodeToComparable());
      return Status::OK();
    }

    case ValueEntryType::kVarInt: {
      util::VarInt varint;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(varint.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      type_ = value_type;
      new(&str_val_) string(varint.EncodeToComparable());
      return Status::OK();
    }

    case ValueEntryType::kTimestamp:
      if (slice.size() != sizeof(Timestamp)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      timestamp_val_ = Timestamp(BigEndian::Load64(slice.data()));
      return Status::OK();

    case ValueEntryType::kJsonb: {
      if (slice.size() < sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
                             value_type, slice.size());
      }
      // Read the jsonb flags.
      int64_t jsonb_flags = BigEndian::Load64(slice.data());
      slice.remove_prefix(sizeof(jsonb_flags));

      // Read the serialized jsonb.
      new(&str_val_) string(slice.ToBuffer());
      type_ = value_type;
      return Status::OK();
    }

    case ValueEntryType::kInetaddress: {
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

    case ValueEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTableId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUuid: {
      if (slice.size() != kUuidSize) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes to decode Uuid: $0, need $1",
            slice.size(), kUuidSize);
      }
      new(&uuid_val_) Uuid(VERIFY_RESULT(Uuid::FromComparable(slice)));
      type_ = value_type;
      return Status::OK();
    }
    case ValueEntryType::kRowLock: {
      type_ = value_type;
      return Status::OK();
    }

    case ValueEntryType::kInvalid: FALLTHROUGH_INTENDED;
    case ValueEntryType::kPackedRow: FALLTHROUGH_INTENDED;
    case ValueEntryType::kMaxByte:
      return STATUS_FORMAT(Corruption, "$0 is not allowed in a RocksDB PrimitiveValue", value_type);
  }
  RSTATUS_DCHECK(
      false, Corruption, "Wrong value type $0 in $1", value_type, rocksdb_slice.ToDebugHexString());
}

Status PrimitiveValue::DecodeToQLValuePB(
    const Slice& rocksdb_slice, const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_value) {

  RSTATUS_DCHECK(!rocksdb_slice.empty(), Corruption, "Cannot decode a value from an empty slice");
  Slice slice(rocksdb_slice);

  const auto value_type = ConsumeValueEntryType(&slice);
  switch (value_type) {
    case ValueEntryType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueEntryType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTombstone:
      SetNull(ql_value);
      return Status::OK();

    case ValueEntryType::kFalse: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTrue:
      if (ql_type->main() != DataType::BOOL) {
        break;
      }
      ql_value->set_bool_value(IsTrue(value_type));
      return Status::OK();

    case ValueEntryType::kInt32: FALLTHROUGH_INTENDED;
    case ValueEntryType::kWriteId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kFloat: {
      if (slice.size() != sizeof(int32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      int32_t int32_val = BigEndian::Load32(slice.data());
      if (ql_type->main() == DataType::INT8) {
        ql_value->set_int8_value(static_cast<int8_t>(int32_val));
      } else if (ql_type->main() == DataType::INT16) {
        ql_value->set_int16_value(static_cast<int16_t>(int32_val));
      } else if (ql_type->main() == DataType::INT32) {
        ql_value->set_int32_value(int32_val);
      } else if (ql_type->main() == DataType::FLOAT) {
        ql_value->set_float_value(bit_cast<float>(int32_val));
      } else {
        break;
      }
      return Status::OK();
    }

    case ValueEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUInt32: FALLTHROUGH_INTENDED;
    case ValueEntryType::kSubTransactionId: {
      if (slice.size() != sizeof(uint32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      uint32_t uint32_val = BigEndian::Load32(slice.data());
      if (ql_type->main() == DataType::UINT32) {
        ql_value->set_uint32_value(uint32_val);
      } else if (ql_type->main() == DataType::DATE) {
        ql_value->set_date_value(uint32_val);
      } else {
        break;
      }
      return Status::OK();
    }

    case ValueEntryType::kInt64: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArrayIndex: FALLTHROUGH_INTENDED;
    case ValueEntryType::kDouble: {
      if (slice.size() != sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      int64_t int64_val = BigEndian::Load64(slice.data());
      if (ql_type->main() == DataType::INT64) {
        ql_value->set_int64_value(int64_val);
      } else if (ql_type->main() == DataType::TIME) {
        ql_value->set_time_value(int64_val);
      } else if (ql_type->main() == DataType::DOUBLE) {
        ql_value->set_double_value(bit_cast<double>(int64_val));
      } else {
        break;
      }
      return Status::OK();
    }

    case ValueEntryType::kUInt64:
      if (slice.size() != sizeof(uint64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      if (ql_type->main() != DataType::UINT64) {
        break;
      }

      ql_value->set_uint64_value(BigEndian::Load64(slice.data()));
      return Status::OK();

    case ValueEntryType::kDecimal: {
      // TODO(kpopali): check if we can simply put the slice value, instead of decoding and
      // encoding it again.
      util::Decimal decimal;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      if (ql_type->main() != DataType::DECIMAL) {
        break;
      }

      ql_value->set_decimal_value(decimal.EncodeToComparable());
      return Status::OK();
    }

    case ValueEntryType::kVarInt: {
      // TODO(kpopali): check if we can simply put the slice value, instead of decoding and
      // encoding it again.
      util::VarInt varint;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(varint.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      if (ql_type->main() != DataType::VARINT) {
        break;
      }

      ql_value->set_varint_value(varint.EncodeToComparable());
      return Status::OK();
    }

    case ValueEntryType::kTimestamp: {
      if (slice.size() != sizeof(Timestamp)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      Timestamp timestamp_val = Timestamp(BigEndian::Load64(slice.data()));
      if (ql_type->main() == DataType::TIMESTAMP) {
        ql_value->set_timestamp_value(timestamp_val.ToInt64());
      } else {
        break;
      }
      return Status::OK();
    }

    case ValueEntryType::kCollString: FALLTHROUGH_INTENDED;
    case ValueEntryType::kString:
      if (ql_type->main() == DataType::STRING) {
        ql_value->set_string_value(slice.cdata(), slice.size());
      } else if (ql_type->main() == DataType::BINARY) {
        ql_value->set_binary_value(slice.cdata(), slice.size());
      } else {
        break;
      }
      return Status::OK();

    case ValueEntryType::kInetaddress: {
      if (slice.size() != kInetAddressV4Size && slice.size() != kInetAddressV6Size) {
        return STATUS_FORMAT(Corruption,
                             "Invalid number of bytes to decode IPv4/IPv6: $0, need $1 or $2",
                             slice.size(), kInetAddressV4Size, kInetAddressV6Size);
      }
      // Need to use a non-rocksdb slice for InetAddress.
      Slice slice_temp(slice.data(), slice.size());
      InetAddress inetaddress_val;
      RETURN_NOT_OK(inetaddress_val.FromSlice(slice_temp));
      if (ql_type->main() != DataType::INET) {
        break;
      }

      QLValue::set_inetaddress_value(inetaddress_val, ql_value);
      return Status::OK();
    }

    case ValueEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTableId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUuid: {
      if (slice.size() != kUuidSize) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes to decode Uuid: $0, need $1",
            slice.size(), kUuidSize);
      }
      Uuid uuid = VERIFY_RESULT(Uuid::FromComparable(slice));
      if (ql_type->main() == DataType::UUID) {
        QLValue::set_uuid_value(uuid, ql_value);
      } else if (ql_type->main() == DataType::TIMEUUID) {
        QLValue::set_timeuuid_value(uuid, ql_value);
      } else {
        break;
      }
      return Status::OK();
    }

    case ValueEntryType::kJsonb: {
      if (slice.size() < sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
                             value_type, slice.size());
      }
      // Read the jsonb flags.
      int64_t jsonb_flags = BigEndian::Load64(slice.data());
      slice.remove_prefix(sizeof(jsonb_flags));

      if (ql_type->main() != DataType::JSONB) {
        break;
      }

      ql_value->set_jsonb_value(slice.data(), slice.size());
      return Status::OK();
    }

    case ValueEntryType::kFrozen: {
      // TODO(kpopali): make sure that PGSQL doesn't use PGSQL type.
      break;
    }

    case ValueEntryType::kObject: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArray: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisList: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisTS: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisSortedSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kGinNull:
      break;

    case ValueEntryType::kInvalid: FALLTHROUGH_INTENDED;
    case ValueEntryType::kPackedRow: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRowLock: FALLTHROUGH_INTENDED;
    case ValueEntryType::kMaxByte:
      return STATUS_FORMAT(Corruption, "$0 is not allowed in a RocksDB PrimitiveValue", value_type);
  }

  RSTATUS_DCHECK(
      false, Corruption, "Wrong value type $0 in $1 OR unsupported datatype $2",
      value_type, rocksdb_slice.ToDebugHexString(), ql_type->main());
}

POD_FACTORY(Double, double);
POD_FACTORY(Float, float);

PrimitiveValue PrimitiveValue::Decimal(const Slice& decimal_str) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueEntryType::kDecimal;
  new(&primitive_value.str_val_) string(decimal_str.cdata(), decimal_str.size());
  return primitive_value;
}

KeyEntryValue KeyEntryValue::Decimal(const Slice& decimal_str, SortOrder sort_order) {
  KeyEntryValue primitive_value;
  primitive_value.type_ = SELECT_VALUE_TYPE(Decimal, sort_order);
  new(&primitive_value.str_val_) string(decimal_str.cdata(), decimal_str.size());
  return primitive_value;
}

PrimitiveValue PrimitiveValue::VarInt(const Slice& varint_str) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueEntryType::kVarInt;
  new(&primitive_value.str_val_) string(varint_str.cdata(), varint_str.size());
  return primitive_value;
}

KeyEntryValue KeyEntryValue::VarInt(const Slice& varint_str, SortOrder sort_order) {
  KeyEntryValue primitive_value;
  primitive_value.type_ = SELECT_VALUE_TYPE(VarInt, sort_order);
  new(&primitive_value.str_val_) string(varint_str.cdata(), varint_str.size());
  return primitive_value;
}

KeyEntryValue KeyEntryValue::ArrayIndex(int64_t index) {
  KeyEntryValue primitive_value;
  primitive_value.type_ = KeyEntryType::kArrayIndex;
  primitive_value.int64_val_ = index;
  return primitive_value;
}

KeyEntryValue KeyEntryValue::UInt16Hash(uint16_t hash) {
  KeyEntryValue primitive_value;
  primitive_value.type_ = KeyEntryType::kUInt16Hash;
  primitive_value.uint16_val_ = hash;
  return primitive_value;
}

KeyEntryValue KeyEntryValue::SystemColumnId(SystemColumnIds system_column_id) {
  return KeyEntryValue::SystemColumnId(ColumnId(static_cast<ColumnIdRep>(system_column_id)));
}

KeyEntryValue KeyEntryValue::SystemColumnId(ColumnId column_id) {
  KeyEntryValue primitive_value;
  primitive_value.type_ = KeyEntryType::kSystemColumnId;
  primitive_value.column_id_val_ = column_id;
  return primitive_value;
}

KeyEntryValue KeyEntryValue::MakeColumnId(ColumnId column_id) {
  KeyEntryValue primitive_value;
  primitive_value.type_ = KeyEntryType::kColumnId;
  primitive_value.column_id_val_ = column_id;
  return primitive_value;
}

POD_FACTORY(Int32, int32)
POD_FACTORY(UInt32, uint32)
POD_FACTORY(Int64, int64);
POD_FACTORY(UInt64, uint64);

PrimitiveValue PrimitiveValue::Jsonb(const Slice& json) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueEntryType::kJsonb;
  new(&primitive_value.str_val_) string(json.cdata(), json.size());
  return primitive_value;
}

PrimitiveValue PrimitiveValue::GinNull(uint8_t v) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueEntryType::kGinNull;
  primitive_value.gin_null_val_ = v;
  return primitive_value;
}

KeyBytes KeyEntryValue::ToKeyBytes() const {
  KeyBytes kb;
  AppendToKey(&kb);
  return kb;
}

bool PrimitiveValue::operator==(const PrimitiveValue& other) const {
  if (type_ != other.type_) {
    return false;
  }
  switch (type_) {
    case ValueEntryType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueEntryType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueEntryType::kFalse: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTrue: FALLTHROUGH_INTENDED;
    case ValueEntryType::kMaxByte: return true;

    case ValueEntryType::kCollString: FALLTHROUGH_INTENDED;
    case ValueEntryType::kString: return str_val_ == other.str_val_;

    case ValueEntryType::kFrozen: return *frozen_val_ == *other.frozen_val_;

    case ValueEntryType::kWriteId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kInt32: return int32_val_ == other.int32_val_;

    case ValueEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUInt32: return uint32_val_ == other.uint32_val_;

    case ValueEntryType::kUInt64: return uint64_val_ == other.uint64_val_;

    case ValueEntryType::kInt64: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArrayIndex: return int64_val_ == other.int64_val_;

    case ValueEntryType::kFloat: {
      if (util::IsNanFloat(float_val_) && util::IsNanFloat(other.float_val_)) {
        return true;
      }
      return float_val_ == other.float_val_;
    }
    case ValueEntryType::kDouble: {
      if (util::IsNanDouble(double_val_) && util::IsNanDouble(other.double_val_)) {
        return true;
      }
      return double_val_ == other.double_val_;
    }
    case ValueEntryType::kDecimal: return str_val_ == other.str_val_;
    case ValueEntryType::kVarInt: return str_val_ == other.str_val_;

    case ValueEntryType::kTimestamp: return timestamp_val_ == other.timestamp_val_;
    case ValueEntryType::kInetaddress: return *inetaddress_val_ == *(other.inetaddress_val_);
    case ValueEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTableId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUuid: return uuid_val_ == other.uuid_val_;

    case ValueEntryType::kGinNull: return gin_null_val_ == other.gin_null_val_;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  FATAL_INVALID_ENUM_VALUE(ValueEntryType, type_);
}

int PrimitiveValue::CompareTo(const PrimitiveValue& other) const {
  int result = CompareUsingLessThan(type_, other.type_);
  if (result != 0) {
    return result;
  }
  switch (type_) {
    case ValueEntryType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueEntryType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueEntryType::kFalse: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTrue: FALLTHROUGH_INTENDED;
    case ValueEntryType::kMaxByte:
      return 0;
    case ValueEntryType::kCollString: FALLTHROUGH_INTENDED;
    case ValueEntryType::kDecimal: FALLTHROUGH_INTENDED;
    case ValueEntryType::kVarInt: FALLTHROUGH_INTENDED;
    case ValueEntryType::kString:
      return str_val_.compare(other.str_val_);
    case ValueEntryType::kInt32: FALLTHROUGH_INTENDED;
    case ValueEntryType::kWriteId:
      return CompareUsingLessThan(int32_val_, other.int32_val_);
    case ValueEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUInt32:
      return CompareUsingLessThan(uint32_val_, other.uint32_val_);
    case ValueEntryType::kUInt64:
      return CompareUsingLessThan(uint64_val_, other.uint64_val_);
    case ValueEntryType::kInt64: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArrayIndex:
      return CompareUsingLessThan(int64_val_, other.int64_val_);
    case ValueEntryType::kDouble:
      return CompareUsingLessThan(double_val_, other.double_val_);
    case ValueEntryType::kFloat:
      return CompareUsingLessThan(float_val_, other.float_val_);
    case ValueEntryType::kTimestamp:
      return CompareUsingLessThan(timestamp_val_, other.timestamp_val_);
    case ValueEntryType::kInetaddress:
      return CompareUsingLessThan(*inetaddress_val_, *(other.inetaddress_val_));
    case ValueEntryType::kFrozen: {
      // Compare elements one by one.
      size_t min_size = std::min(frozen_val_->size(), other.frozen_val_->size());
      for (size_t i = 0; i < min_size; i++) {
        result = frozen_val_->at(i).CompareTo(other.frozen_val_->at(i));
        if (result != 0) {
          return result;
        }
      }

      // If elements are equal, compare lengths.
      return CompareUsingLessThan(frozen_val_->size(), other.frozen_val_->size());
    }
    case ValueEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTableId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kUuid:
      return CompareUsingLessThan(uuid_val_, other.uuid_val_);
    case ValueEntryType::kGinNull:
      return CompareUsingLessThan(gin_null_val_, other.gin_null_val_);
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  LOG(FATAL) << "Comparing invalid PrimitiveValues: " << *this << " and " << other;
}

PrimitiveValue::PrimitiveValue() : type_(ValueEntryType::kNullLow) {
}

// This is used to initialize kNullLow, kNullHigh, kTrue, kFalse constants.
PrimitiveValue::PrimitiveValue(ValueEntryType value_type)
    : type_(value_type) {
  complex_data_structure_ = nullptr;
  if (IsStoredAsString()) {
    new(&str_val_) std::string();
  } else if (value_type == ValueEntryType::kInetaddress) {
    inetaddress_val_ = new InetAddress();
  } else if (value_type == ValueEntryType::kUuid) {
    new(&uuid_val_) Uuid();
  } else if (value_type == ValueEntryType::kFrozen) {
    frozen_val_ = new FrozenContainer();
  }
}

PrimitiveValue::PrimitiveValue(const PrimitiveValue& other) {
  if (other.IsStoredAsString()) {
    type_ = other.type_;
    new(&str_val_) std::string(other.str_val_);
  } else if (other.type_ == ValueEntryType::kInetaddress) {
    type_ = other.type_;
    inetaddress_val_ = new InetAddress(*(other.inetaddress_val_));
  } else if (other.type_ == ValueEntryType::kUuid) {
    type_ = other.type_;
    new(&uuid_val_) Uuid(std::move((other.uuid_val_)));
  } else if (other.type_ == ValueEntryType::kFrozen) {
    type_ = other.type_;
    frozen_val_ = new FrozenContainer(*(other.frozen_val_));
  } else {
    memmove(static_cast<void*>(this), &other, sizeof(PrimitiveValue));
  }
  ttl_seconds_ = other.ttl_seconds_;
  write_time_ = other.write_time_;
}

ValueEntryType StringType(bool is_collate) {
  return is_collate ? ValueEntryType::kCollString : ValueEntryType::kString;
}

PrimitiveValue::PrimitiveValue(const Slice& s, bool is_collate) : type_(StringType(is_collate)) {
  new(&str_val_) std::string(s.cdata(), s.cend());
}

PrimitiveValue::PrimitiveValue(const std::string& s, bool is_collate)
    : type_(StringType(is_collate)) {
  new(&str_val_) std::string(s);
}

PrimitiveValue::PrimitiveValue(const char* s, bool is_collate)
    : type_(StringType(is_collate)) {
  new(&str_val_) std::string(s);
}

PrimitiveValue::PrimitiveValue(const Timestamp& timestamp)
    : type_(ValueEntryType::kTimestamp) {
  timestamp_val_ = timestamp;
}

PrimitiveValue::PrimitiveValue(const InetAddress& inetaddress)
    : type_(ValueEntryType::kInetaddress) {
  inetaddress_val_ = new InetAddress(inetaddress);
}

PrimitiveValue::PrimitiveValue(const Uuid& uuid)
    : type_(ValueEntryType::kUuid) {
  uuid_val_ = uuid;
}

KeyEntryValue::KeyEntryValue(const DocHybridTime& hybrid_time)
    : type_(KeyEntryType::kHybridTime), hybrid_time_val_(hybrid_time) {
}

KeyEntryValue::KeyEntryValue(const HybridTime& hybrid_time)
    : KeyEntryValue(DocHybridTime(hybrid_time)) {
}

PrimitiveValue::~PrimitiveValue() {
  if (IsStoredAsString()) {
    str_val_.~basic_string();
  } else if (type_ == ValueEntryType::kInetaddress) {
    delete inetaddress_val_;
  } else if (type_ == ValueEntryType::kFrozen) {
    delete frozen_val_;
  }
  // HybridTime does not need its destructor to be called, because it is a simple wrapper over an
  // unsigned 64-bit integer.
}

KeyEntryValue KeyEntryValue::NullValue(SortingType sorting) {
  return KeyEntryValue(
      sorting == SortingType::kAscendingNullsLast || sorting == SortingType::kDescendingNullsLast
      ? KeyEntryType::kNullHigh
      : KeyEntryType::kNullLow);
}

bool PrimitiveValue::IsPrimitive() const {
  return IsPrimitiveValueType(type_);
}

bool PrimitiveValue::IsTombstone() const {
  return type_ == ValueEntryType::kTombstone;
}

bool PrimitiveValue::IsTombstoneOrPrimitive() const {
  return IsPrimitive() || IsTombstone();
}

bool KeyEntryValue::IsInfinity() const {
  return type_ == KeyEntryType::kHighest || type_ == KeyEntryType::kLowest;
}

bool PrimitiveValue::IsInt64() const {
  return ValueEntryType::kInt64 == type_;
}

bool PrimitiveValue::IsString() const {
  return ValueEntryType::kString == type_ || ValueEntryType::kCollString == type_;
}

bool PrimitiveValue::IsStoredAsString() const {
  return IsString() || type_ == ValueEntryType::kJsonb || type_ == ValueEntryType::kDecimal ||
         type_ == ValueEntryType::kVarInt;
}

bool PrimitiveValue::IsDouble() const {
  return ValueEntryType::kDouble == type_;
}

const std::string& PrimitiveValue::GetString() const {
  DCHECK(IsStoredAsString()) << "Actual type: " << type_;
  return str_val_;
}

int32_t PrimitiveValue::GetInt32() const {
  DCHECK_EQ(ValueEntryType::kInt32, type_);
  return int32_val_;
}

uint32_t PrimitiveValue::GetUInt32() const {
  DCHECK_EQ(ValueEntryType::kUInt32, type_);
  return uint32_val_;
}

int64_t PrimitiveValue::GetInt64() const {
  DCHECK_EQ(ValueEntryType::kInt64, type_);
  return int64_val_;
}

uint64_t PrimitiveValue::GetUInt64() const {
  DCHECK_EQ(ValueEntryType::kUInt64, type_);
  return uint64_val_;
}

float PrimitiveValue::GetFloat() const {
  DCHECK_EQ(ValueEntryType::kFloat, type_);
  return float_val_;
}

bool PrimitiveValue::GetBoolean() const {
  DCHECK(type_ == ValueEntryType::kTrue || type_ == ValueEntryType::kFalse);
  return type_ == ValueEntryType::kTrue ? true : false;
}

const std::string& PrimitiveValue::GetDecimal() const {
  DCHECK_EQ(ValueEntryType::kDecimal, type_);
  return str_val_;
}

const std::string& PrimitiveValue::GetVarInt() const {
  DCHECK_EQ(ValueEntryType::kVarInt, type_);
  return str_val_;
}

Timestamp PrimitiveValue::GetTimestamp() const {
  DCHECK_EQ(ValueEntryType::kTimestamp, type_);
  return timestamp_val_;
}

const InetAddress& PrimitiveValue::GetInetAddress() const {
  DCHECK_EQ(type_, ValueEntryType::kInetaddress);
  return *inetaddress_val_;
}

const std::string& PrimitiveValue::GetJson() const {
  DCHECK_EQ(type_, ValueEntryType::kJsonb);
  return str_val_;
}

const FrozenContainer& PrimitiveValue::GetFrozen() const {
  DCHECK_EQ(type_, ValueEntryType::kFrozen);
  return *frozen_val_;
}

const Uuid& PrimitiveValue::GetUuid() const {
  DCHECK(type_ == ValueEntryType::kUuid ||
         type_ == ValueEntryType::kTransactionId || type_ == ValueEntryType::kTableId);
  return uuid_val_;
}

uint8_t PrimitiveValue::GetGinNull() const {
  DCHECK(ValueEntryType::kGinNull == type_);
  return gin_null_val_;
}

void PrimitiveValue::MoveFrom(PrimitiveValue* other) {
  if (this == other) {
    return;
  }

  ttl_seconds_ = other->ttl_seconds_;
  write_time_ = other->write_time_;
  if (other->IsStoredAsString()) {
    type_ = other->type_;
    new(&str_val_) std::string(std::move(other->str_val_));
    // The moved-from object should now be in a "valid but unspecified" state as per the standard.
  } else if (other->type_ == ValueEntryType::kInetaddress) {
    type_ = other->type_;
    inetaddress_val_ = new InetAddress(std::move(*(other->inetaddress_val_)));
  } else if (other->type_ == ValueEntryType::kUuid) {
    type_ = other->type_;
    new(&uuid_val_) Uuid(std::move((other->uuid_val_)));
  } else if (other->type_ == ValueEntryType::kFrozen) {
    type_ = other->type_;
    frozen_val_ = new FrozenContainer(std::move(*(other->frozen_val_)));
  } else {
    // Non-string primitive values only have plain old data. We are assuming there is no overlap
    // between the two objects, so we're using memcpy instead of memmove.
    memcpy(static_cast<void*>(this), other, sizeof(PrimitiveValue));
#ifndef NDEBUG
    // We could just leave the old object as is for it to be in a "valid but unspecified" state.
    // However, in debug mode we clear the old object's state to make sure we don't attempt to use
    // it.
    memset(static_cast<void*>(other), 0xab, sizeof(PrimitiveValue));
    // Restore the type. There should be no deallocation for non-string types anyway.
    other->type_ = ValueEntryType::kNullLow;
#endif
  }
}

SortOrder SortOrderFromColumnSchemaSortingType(SortingType sorting_type) {
  if (sorting_type == SortingType::kDescending ||
      sorting_type == SortingType::kDescendingNullsLast) {
    return SortOrder::kDescending;
  }
  return SortOrder::kAscending;
}

PrimitiveValue PrimitiveValue::FromQLValuePB(const LWQLValuePB& value) {
  return DoFromQLValuePB(value);
}

PrimitiveValue PrimitiveValue::FromQLValuePB(const QLValuePB& value) {
  return DoFromQLValuePB(value);
}

template <class PB>
PrimitiveValue PrimitiveValue::DoFromQLValuePB(const PB& value) {
  switch (value.value_case()) {
    case QLValuePB::kInt8Value:
      return PrimitiveValue::Int32(value.int8_value());
    case QLValuePB::kInt16Value:
      return PrimitiveValue::Int32(value.int16_value());
    case QLValuePB::kInt32Value:
      return PrimitiveValue::Int32(value.int32_value());
    case QLValuePB::kInt64Value:
      return PrimitiveValue::Int64(value.int64_value());
    case QLValuePB::kUint32Value:
      return PrimitiveValue::UInt32(value.uint32_value());
    case QLValuePB::kUint64Value:
      return PrimitiveValue::UInt64(value.uint64_value());
    case QLValuePB::kFloatValue:
      return PrimitiveValue::Float(util::CanonicalizeFloat(value.float_value()));
    case QLValuePB::kDoubleValue:
      return PrimitiveValue::Double(util::CanonicalizeDouble(value.double_value()));
    case QLValuePB::kDecimalValue:
      return PrimitiveValue::Decimal(value.decimal_value());
    case QLValuePB::kVarintValue:
      return PrimitiveValue::VarInt(value.varint_value());
    case QLValuePB::kStringValue: {
      const auto& val = value.string_value();
      return PrimitiveValue(val, IsCollationEncodedString(val));
    }
    case QLValuePB::kBinaryValue:
      // TODO consider using dedicated encoding for binary (not string) to avoid overhead of
      // zero-encoding for keys (since zero-bytes could be common for binary)
      return PrimitiveValue(value.binary_value());
    case QLValuePB::kBoolValue:
      return PrimitiveValue(value.bool_value() ? ValueEntryType::kTrue : ValueEntryType::kFalse);
    case QLValuePB::kTimestampValue:
      return PrimitiveValue(QLValue::timestamp_value(value));
    case QLValuePB::kDateValue:
      return PrimitiveValue::UInt32(value.date_value());
    case QLValuePB::kTimeValue:
      return PrimitiveValue::Int64(value.time_value());
    case QLValuePB::kInetaddressValue:
      return PrimitiveValue(QLValue::inetaddress_value(value));
    case QLValuePB::kJsonbValue:
      return PrimitiveValue::Jsonb(QLValue::jsonb_value(value));
    case QLValuePB::kUuidValue:
      return PrimitiveValue(QLValue::uuid_value(value));
    case QLValuePB::kTimeuuidValue:
      return PrimitiveValue(QLValue::timeuuid_value(value));
    case QLValuePB::kFrozenValue: {
      const auto& frozen = value.frozen_value();
      PrimitiveValue pv(ValueEntryType::kFrozen);
      auto null_value_type = KeyEntryType::kNullLow;

      for (const auto& elem : frozen.elems()) {
        if (IsNull(elem)) {
          pv.frozen_val_->emplace_back(null_value_type);
        } else {
          pv.frozen_val_->push_back(KeyEntryValue::FromQLValuePB(elem, SortingType::kNotSpecified));
        }
      }
      return pv;
    }
    case QLValuePB::VALUE_NOT_SET:
      return PrimitiveValue::kTombstone;

    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kTupleValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      break;

    case QLValuePB::kVirtualValue:
      return PrimitiveValue(VirtualValueToValueEntryType(value.virtual_value()));
    case QLValuePB::kGinNullValue:
      return PrimitiveValue::GinNull(value.gin_null_value());

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype in PrimitiveValue: " << value.value_case();
}

namespace {

template <class Value>
bool SharedToQLValuePB(
    const Value& value, const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_value) {
  // DocDB sets type to kInvalidValueType for SubDocuments that don't exist. That's why they need
  // to be set to Null in QLValue.
  auto type = value.type();
  if (type == Value::Type::kNullLow || type == Value::Type::kNullHigh ||
      type == Value::Type::kInvalid) {
    SetNull(ql_value);
    return true;
  }

  // For ybgin indexes, null category can be set on any index key column, regardless of the column's
  // actual type.  The column's actual type cannot be kGinNull, so it throws error in the below
  // switch.
  if (type == Value::Type::kGinNull) {
    ql_value->set_gin_null_value(value.GetGinNull());
    return true;
  }

  switch (ql_type->main()) {
    case INT8:
      ql_value->set_int8_value(static_cast<int8_t>(value.GetInt32()));
      return true;
    case INT16:
      ql_value->set_int16_value(static_cast<int16_t>(value.GetInt32()));
      return true;
    case INT32:
      ql_value->set_int32_value(value.GetInt32());
      return true;
    case INT64:
      ql_value->set_int64_value(value.GetInt64());
      return true;
    case UINT32:
      ql_value->set_uint32_value(value.GetUInt32());
      return true;
    case UINT64:
      ql_value->set_uint64_value(value.GetUInt64());
      return true;
    case FLOAT:
      ql_value->set_float_value(value.GetFloat());
      return true;
    case DOUBLE:
      ql_value->set_double_value(value.GetDouble());
      return true;
    case DECIMAL:
      ql_value->set_decimal_value(value.GetDecimal());
      return true;
    case VARINT:
      ql_value->set_varint_value(value.GetVarInt());
      return true;
    case BOOL:
      ql_value->set_bool_value(IsTrue(type));
      return true;
    case TIMESTAMP:
      ql_value->set_timestamp_value(value.GetTimestamp().ToInt64());
      return true;
    case DATE:
      ql_value->set_date_value(value.GetUInt32());
      return true;
    case TIME:
      ql_value->set_time_value(value.GetInt64());
      return true;
    case INET: {
      QLValue::set_inetaddress_value(value.GetInetAddress(), ql_value);
      return true;
    }
    case UUID:
      QLValue::set_uuid_value(value.GetUuid(), ql_value);
      return true;
    case TIMEUUID:
      QLValue::set_timeuuid_value(value.GetUuid(), ql_value);
      return true;
    case STRING:
      ql_value->set_string_value(value.GetString());
      return true;
    case BINARY:
      ql_value->set_binary_value(value.GetString());
      return true;
    case FROZEN: {
      const auto& type = ql_type->param_type(0);
      QLSeqValuePB *frozen_value = ql_value->mutable_frozen_value();
      frozen_value->clear_elems();
      const auto& frozen = value.GetFrozen();
      switch (type->main()) {
        case MAP: {
          const std::shared_ptr<QLType>& keys_type = type->param_type(0);
          const std::shared_ptr<QLType>& values_type = type->param_type(1);
          for (size_t i = 0; i < frozen.size(); i++) {
            if (i % 2 == 0) {
              frozen[i].ToQLValuePB(keys_type, frozen_value->add_elems());
            } else {
              frozen[i].ToQLValuePB(values_type, frozen_value->add_elems());
            }
          }
          return true;
        }
        case SET: FALLTHROUGH_INTENDED;
        case LIST: {
          const std::shared_ptr<QLType>& elems_type = type->param_type(0);
          for (const auto &pv : frozen) {
            QLValuePB *elem = frozen_value->add_elems();
            pv.ToQLValuePB(elems_type, elem);
          }
          return true;
        }
        case USER_DEFINED_TYPE: {
          for (size_t i = 0; i < frozen.size(); i++) {
            frozen[i].ToQLValuePB(type->param_type(i), frozen_value->add_elems());
          }
          return true;
        }

        default:
          break;
      }
      FATAL_INVALID_ENUM_VALUE(DataType, type->main());
    }

    case JSONB: FALLTHROUGH_INTENDED;
    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case MAP: FALLTHROUGH_INTENDED;
    case SET: FALLTHROUGH_INTENDED;
    case LIST: FALLTHROUGH_INTENDED;
    case TUPLE: FALLTHROUGH_INTENDED;
    case TYPEARGS: FALLTHROUGH_INTENDED;
    case USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;

    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case GIN_NULL:
      break;

    // default: fall through
  }

  return false;
}

} // namespace

void PrimitiveValue::ToQLValuePB(const std::shared_ptr<QLType>& ql_type,
                                 QLValuePB* ql_value) const {
  if (type() == ValueEntryType::kTombstone) {
    SetNull(ql_value);
    return;
  }

  if (SharedToQLValuePB(*this, ql_type, ql_value)) {
    return;
  }

  if (ql_type->main() == DataType::JSONB) {
    QLValue temp_value;
    temp_value.set_jsonb_value(GetJson());
    *ql_value = std::move(*temp_value.mutable_value());
    return;
  }

  LOG(FATAL) << "Unsupported datatype " << ql_type->ToString();
}

KeyEntryValue::KeyEntryValue() : type_(KeyEntryType::kInvalid) {
}

KeyEntryValue::KeyEntryValue(KeyEntryType type) : type_(type) {
  if (IsStoredAsString()) {
    new(&str_val_) std::string();
  } else if (IsInetAddress()) {
    inetaddress_val_ = new InetAddress();
  } else if (IsUuid()) {
    new(&uuid_val_) Uuid();
  } else if (IsFrozen()) {
    frozen_val_ = new FrozenContainer();
  }
}

KeyEntryValue::KeyEntryValue(KeyEntryValue&& rhs) : type_(rhs.type_) {
  if (rhs.IsStoredAsString()) {
    new(&str_val_) std::string(std::move(rhs.str_val_));
  } else if (rhs.IsInetAddress()) {
    inetaddress_val_ = rhs.inetaddress_val_;
  } else if (rhs.IsUuid()) {
    new(&uuid_val_) Uuid(std::move(rhs.uuid_val_));
  } else if (rhs.IsFrozen()) {
    frozen_val_ = rhs.frozen_val_;
  } else {
    memmove(static_cast<void*>(this), &rhs, sizeof(*this));
  }
  rhs.type_ = KeyEntryType::kInvalid;
}

KeyEntryValue::KeyEntryValue(const KeyEntryValue& rhs) : type_(rhs.type_) {
  if (rhs.IsStoredAsString()) {
    new(&str_val_) std::string(rhs.str_val_);
  } else if (rhs.IsInetAddress()) {
    inetaddress_val_ = new InetAddress(*rhs.inetaddress_val_);
  } else if (rhs.IsUuid()) {
    new(&uuid_val_) Uuid(rhs.uuid_val_);
  } else if (rhs.IsFrozen()) {
    frozen_val_ = new FrozenContainer(*rhs.frozen_val_);
  } else {
    memmove(static_cast<void*>(this), &rhs, sizeof(*this));
  }
}

KeyEntryType StringType(SortOrder sort_order, bool is_collate) {
  return is_collate ? SELECT_VALUE_TYPE(CollString, sort_order)
                    : SELECT_VALUE_TYPE(String, sort_order);
}

KeyEntryValue::KeyEntryValue(
    const Slice& str, SortOrder sort_order, bool is_collate)
        : type_(StringType(sort_order, is_collate)) {
  new(&str_val_) std::string(str.cdata(), str.size());
}

KeyEntryValue& KeyEntryValue::operator =(const KeyEntryValue& rhs) {
  if (rhs.IsStoredAsString()) {
    if (IsStoredAsString()) {
      str_val_ = rhs.str_val_;
    } else {
      Destroy();
      new(&str_val_) std::string(rhs.str_val_);
    }
  } else if (rhs.IsInetAddress()) {
    if (IsInetAddress()) {
      *inetaddress_val_ = *rhs.inetaddress_val_;
    } else {
      Destroy();
      inetaddress_val_ = new InetAddress(*rhs.inetaddress_val_);
    }
  } else if (rhs.IsUuid()) {
    if (IsUuid()) {
      uuid_val_ = rhs.uuid_val_;
    } else {
      Destroy();
      new(&uuid_val_) Uuid(rhs.uuid_val_);
    }
  } else if (rhs.IsFrozen()) {
    if (IsFrozen()) {
      *frozen_val_ = *rhs.frozen_val_;
    } else {
      Destroy();
      frozen_val_ = new FrozenContainer(*rhs.frozen_val_);
    }
  } else {
    memmove(static_cast<void*>(this), &rhs, sizeof(*this));
    return *this;
  }
  type_ = rhs.type_;
  return *this;
}

KeyEntryValue& KeyEntryValue::operator =(KeyEntryValue&& rhs) {
  if (rhs.IsStoredAsString()) {
    if (IsStoredAsString()) {
      str_val_ = std::move(rhs.str_val_);
    } else {
      Destroy();
      new(&str_val_) std::string(std::move(rhs.str_val_));
    }
    type_ = rhs.type_;
  } else if (rhs.IsInetAddress()) {
    if (IsInetAddress()) {
      *inetaddress_val_ = *rhs.inetaddress_val_;
      type_ = rhs.type_;
    } else {
      Destroy();
      inetaddress_val_ = rhs.inetaddress_val_;
      type_ = rhs.type_;
      rhs.type_ = KeyEntryType::kInvalid;
    }
  } else if (rhs.IsUuid()) {
    if (IsUuid()) {
      uuid_val_ = rhs.uuid_val_;
    } else {
      Destroy();
      new(&uuid_val_) Uuid(std::move(rhs.uuid_val_));
    }
    type_ = rhs.type_;
  } else if (rhs.IsFrozen()) {
    if (IsFrozen()) {
      *frozen_val_ = *rhs.frozen_val_;
      type_ = rhs.type_;
    } else {
      Destroy();
      frozen_val_ = rhs.frozen_val_;
      type_ = rhs.type_;
      rhs.type_ = KeyEntryType::kInvalid;
    }
  } else {
    Destroy();
    memmove(static_cast<void*>(this), &rhs, sizeof(*this));
  }
  rhs.Destroy();
  return *this;
}

KeyEntryValue KeyEntryValue::MakeTimestamp(const Timestamp& timestamp, SortOrder sort_order) {
  KeyEntryValue result;
  result.type_ = SELECT_VALUE_TYPE(Timestamp, sort_order);
  result.timestamp_val_ = timestamp;
  return result;
}

KeyEntryValue KeyEntryValue::MakeInetAddress(const InetAddress& value, SortOrder sort_order) {
  KeyEntryValue result;
  result.type_ = SELECT_VALUE_TYPE(Inetaddress, sort_order);
  result.inetaddress_val_ = new InetAddress(value);
  return result;
}

KeyEntryValue KeyEntryValue::MakeUuid(const Uuid& value, SortOrder sort_order) {
  KeyEntryValue result;
  result.type_ = SELECT_VALUE_TYPE(Uuid, sort_order);
  result.uuid_val_ = value;
  return result;
}

KeyEntryValue KeyEntryValue::GinNull(uint8_t v) {
  KeyEntryValue result;
  result.type_ = KeyEntryType::kGinNull;
  result.gin_null_val_ = v;
  return result;
}

KeyEntryValue::~KeyEntryValue() {
  Destroy();
}

void KeyEntryValue::Destroy() {
  if (IsStoredAsString()) {
    str_val_.~string();
  } else if (IsUuid()) {
    uuid_val_.~Uuid();
  } else if (IsInetAddress()) {
    delete inetaddress_val_;
  } else if (IsFrozen()) {
    delete frozen_val_;
  }
  type_ = KeyEntryType::kInvalid;
}

bool KeyEntryValue::IsStoredAsString() const {
  return IsString()
         || type_ == KeyEntryType::kDecimal || type_ == KeyEntryType::kDecimalDescending
         || type_ == KeyEntryType::kVarInt || type_ == KeyEntryType::kVarIntDescending;
}

bool KeyEntryValue::IsUuid() const {
  return type_ == KeyEntryType::kUuid || type_ == KeyEntryType::kUuidDescending;
}

const Uuid& KeyEntryValue::GetUuid() const {
  DCHECK(IsUuid());
  return uuid_val_;
}

const FrozenContainer& KeyEntryValue::GetFrozen() const {
  DCHECK(IsFrozen());
  return *frozen_val_;
}

bool KeyEntryValue::IsInetAddress() const {
  return type_ == KeyEntryType::kInetaddress || type_ == KeyEntryType::kInetaddressDescending;
}

const InetAddress& KeyEntryValue::GetInetAddress() const {
  DCHECK(IsInetAddress());
  return *inetaddress_val_;
}

bool KeyEntryValue::IsFrozen() const {
  return type_ == KeyEntryType::kFrozen || type_ == KeyEntryType::kFrozenDescending;
}

template <class PB>
KeyEntryValue KeyEntryValue::DoFromQLValuePB(const PB& value, SortingType sorting_type) {
  const auto sort_order = SortOrderFromColumnSchemaSortingType(sorting_type);

  switch (value.value_case()) {
    case QLValuePB::kInt8Value:
      return KeyEntryValue::Int32(value.int8_value(), sort_order);
    case QLValuePB::kInt16Value:
      return KeyEntryValue::Int32(value.int16_value(), sort_order);
    case QLValuePB::kInt32Value:
      return KeyEntryValue::Int32(value.int32_value(), sort_order);
    case QLValuePB::kInt64Value:
      return KeyEntryValue::Int64(value.int64_value(), sort_order);
    case QLValuePB::kUint32Value:
      return KeyEntryValue::UInt32(value.uint32_value(), sort_order);
    case QLValuePB::kUint64Value:
      return KeyEntryValue::UInt64(value.uint64_value(), sort_order);
    case QLValuePB::kFloatValue: {
      float f = value.float_value();
      return KeyEntryValue::Float(util::CanonicalizeFloat(f), sort_order);
    }
    case QLValuePB::kDoubleValue: {
      double d = value.double_value();
      return KeyEntryValue::Double(util::CanonicalizeDouble(d), sort_order);
    }
    case QLValuePB::kDecimalValue:
      return KeyEntryValue::Decimal(value.decimal_value(), sort_order);
    case QLValuePB::kVarintValue:
      return KeyEntryValue::VarInt(value.varint_value(), sort_order);
    case QLValuePB::kStringValue: {
      const auto& val = value.string_value();
      if (sorting_type != SortingType::kNotSpecified && IsCollationEncodedString(val)) {
        return KeyEntryValue(val, sort_order, true /* is_collate */);
      }
      return KeyEntryValue(val, sort_order);
    }
    case QLValuePB::kBinaryValue:
      return KeyEntryValue(value.binary_value(), sort_order);
    case QLValuePB::kBoolValue:
      return KeyEntryValue(sort_order == SortOrder::kDescending
                            ? (value.bool_value() ? KeyEntryType::kTrueDescending
                                                  : KeyEntryType::kFalseDescending)
                            : (value.bool_value() ? KeyEntryType::kTrue
                                                  : KeyEntryType::kFalse));
    case QLValuePB::kTimestampValue:
      return KeyEntryValue::MakeTimestamp(QLValue::timestamp_value(value), sort_order);
    case QLValuePB::kDateValue:
      return KeyEntryValue::UInt32(value.date_value(), sort_order);
    case QLValuePB::kTimeValue:
      return KeyEntryValue::Int64(value.time_value(), sort_order);
    case QLValuePB::kInetaddressValue:
      return KeyEntryValue::MakeInetAddress(QLValue::inetaddress_value(value), sort_order);
    case QLValuePB::kUuidValue:
      return KeyEntryValue::MakeUuid(QLValue::uuid_value(value), sort_order);
    case QLValuePB::kTimeuuidValue:
      return KeyEntryValue::MakeUuid(QLValue::timeuuid_value(value), sort_order);
    case QLValuePB::kFrozenValue: {
      const auto& frozen = value.frozen_value();
      KeyEntryValue pv(KeyEntryType::kFrozen);
      auto null_value_type = KeyEntryType::kNullLow;
      if (sort_order == SortOrder::kDescending) {
        null_value_type = KeyEntryType::kNullHigh;
        pv.type_ = KeyEntryType::kFrozenDescending;
      }

      for (const auto& elem : frozen.elems()) {
        if (IsNull(elem)) {
          pv.frozen_val_->emplace_back(null_value_type);
        } else {
          pv.frozen_val_->push_back(KeyEntryValue::FromQLValuePB(elem, sorting_type));
        }
      }
      return pv;
    }

    case QLValuePB::kVirtualValue:
      return FromQLVirtualValue(value.virtual_value());
    case QLValuePB::kGinNullValue:
      return KeyEntryValue::GinNull(value.gin_null_value());

    case QLValuePB::kJsonbValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kTupleValue: FALLTHROUGH_INTENDED;
    case QLValuePB::VALUE_NOT_SET:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype in PrimitiveValue: " << value.value_case();
}

KeyEntryValue KeyEntryValue::FromQLValuePB(const QLValuePB& value, SortingType sorting_type) {
  return DoFromQLValuePB(value, sorting_type);
}

KeyEntryValue KeyEntryValue::FromQLValuePBForKey(const QLValuePB& value, SortingType sorting_type) {
  if (IsNull(value)) {
    return KeyEntryValue::NullValue(sorting_type);
  }
  return DoFromQLValuePB(value, sorting_type);
}

KeyEntryValue KeyEntryValue::FromQLValuePB(const LWQLValuePB& value, SortingType sorting_type) {
  return DoFromQLValuePB(value, sorting_type);
}

KeyEntryValue KeyEntryValue::FromQLValuePBForKey(
    const LWQLValuePB& value,
    SortingType sorting_type) {
  if (IsNull(value)) {
    return KeyEntryValue::NullValue(sorting_type);
  }
  return DoFromQLValuePB(value, sorting_type);
}

KeyEntryValue KeyEntryValue::FromQLVirtualValue(QLVirtualValuePB value) {
  return KeyEntryValue(VirtualValueToKeyEntryType(value));
}

std::string KeyEntryValue::ToString(AutoDecodeKeys auto_decode_keys) const {
  switch (type_) {
    case KeyEntryType::kNullHigh: FALLTHROUGH_INTENDED;
    case KeyEntryType::kNullLow:
      return "null";
    case KeyEntryType::kGinNull:
      switch (gin_null_val_) {
        // case 0, gin:norm-key, should not exist since the actual data would be used instead.
        case 1:
          return "GinNullKey";
        case 2:
          return "GinEmptyItem";
        case 3:
          return "GinNullItem";
        // case -1, gin:empty-query, should not exist since that's internal to postgres.
        default:
          LOG(FATAL) << "Unexpected gin null category: " << gin_null_val_;
      }
    case KeyEntryType::kCounter:
      return "counter";
    case KeyEntryType::kSSForward:
      return "SSforward";
    case KeyEntryType::kSSReverse:
      return "SSreverse";
    case KeyEntryType::kFalse: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFalseDescending:
      return "false";
    case KeyEntryType::kTrue: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTrueDescending:
      return "true";
    case KeyEntryType::kInvalid:
      return "invalid";
    case KeyEntryType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kCollString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kString:
      if (auto_decode_keys) {
        // This is useful when logging write batches for secondary indexes.
        SubDocKey sub_doc_key;
        Status decode_status = sub_doc_key.FullyDecodeFrom(str_val_, HybridTimeRequired::kFalse);
        if (decode_status.ok()) {
          // This gives us "EncodedSubDocKey(...)".
          return Format("Encoded$0", sub_doc_key);
        }
      }
      return FormatBytesAsStr(str_val_);
    case KeyEntryType::kInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt32:
      return std::to_string(int32_val_);
    case KeyEntryType::kUInt32:
    case KeyEntryType::kUInt32Descending:
      return std::to_string(uint32_val_);
    case KeyEntryType::kUInt64:  FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt64Descending:
      return std::to_string(uint64_val_);
    case KeyEntryType::kInt64Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt64:
      return std::to_string(int64_val_);
    case KeyEntryType::kFloatDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFloat:
      return RealToString(float_val_);
    case KeyEntryType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFrozen:
      return FrozenToString(*frozen_val_);
    case KeyEntryType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDouble:
      return RealToString(double_val_);
    case KeyEntryType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimal:
      return DecimalToString(str_val_);
    case KeyEntryType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kVarInt:
      return VarIntToString(str_val_);
    case KeyEntryType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTimestamp:
      return timestamp_val_.ToString();
    case KeyEntryType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInetaddress:
      return inetaddress_val_->ToString();
    case KeyEntryType::kUuidDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUuid:
      return uuid_val_.ToString();
    case KeyEntryType::kArrayIndex:
      return Substitute("ArrayIndex($0)", int64_val_);
    case KeyEntryType::kHybridTime:
      return hybrid_time_val_.ToString();
    case KeyEntryType::kUInt16Hash:
      return Substitute("UInt16Hash($0)", uint16_val_);
    case KeyEntryType::kColumnId:
      return Format("ColumnId($0)", column_id_val_);
    case KeyEntryType::kSystemColumnId:
      return Format("SystemColumnId($0)", column_id_val_);
    case KeyEntryType::kTableId:
      return Format("TableId($0)", uuid_val_.ToString());
    case KeyEntryType::kColocationId:
      return Format("ColocationId($0)", uint32_val_);
    case KeyEntryType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case KeyEntryType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTransactionId:
      return Substitute("TransactionId($0)", uuid_val_.ToString());
    case KeyEntryType::kSubTransactionId:
      return Substitute("SubTransactionId($0)", uint32_val_);
    case KeyEntryType::kIntentTypeSet:
      return Format("Intents($0)", IntentTypeSet(uint16_val_));
    case KeyEntryType::kObsoleteIntentTypeSet:
      return Format("ObsoleteIntents($0)", uint16_val_);
    case KeyEntryType::kObsoleteIntentType:
      return Format("Intent($0)", uint16_val_);
    case KeyEntryType::kMergeFlags: FALLTHROUGH_INTENDED;
    case KeyEntryType::kBitSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kGroupEnd: FALLTHROUGH_INTENDED;
    case KeyEntryType::kGroupEndDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTtl: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUserTimestamp: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentPrefix: FALLTHROUGH_INTENDED;
    case KeyEntryType::kExternalIntents: FALLTHROUGH_INTENDED;
    case KeyEntryType::kGreaterThanIntentType:
      break;
    case KeyEntryType::kLowest:
      return "-Inf";
    case KeyEntryType::kHighest:
      return "+Inf";
    case KeyEntryType::kMaxByte:
      return "0xff";
  }
  FATAL_INVALID_ENUM_VALUE(KeyEntryType, type_);
}

int KeyEntryValue::CompareTo(const KeyEntryValue& other) const {
  int result = CompareUsingLessThan(type_, other.type_);
  if (result != 0) {
    return result;
  }
  switch (type_) {
    CASE_EMPTY_KEY_ENTRY_TYPES
      return 0;
    case KeyEntryType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kVarIntDescending:
      return other.str_val_.compare(str_val_);
    case KeyEntryType::kCollString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimal: FALLTHROUGH_INTENDED;
    case KeyEntryType::kVarInt:
      return str_val_.compare(other.str_val_);
    case KeyEntryType::kInt64Descending:
      return CompareUsingLessThan(other.int64_val_, int64_val_);
    case KeyEntryType::kInt32Descending:
      return CompareUsingLessThan(other.int32_val_, int32_val_);
    case KeyEntryType::kInt32:
      return CompareUsingLessThan(int32_val_, other.int32_val_);
    case KeyEntryType::kUInt32Descending:
      return CompareUsingLessThan(other.uint32_val_, uint32_val_);
    case KeyEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32:
      return CompareUsingLessThan(uint32_val_, other.uint32_val_);
    case KeyEntryType::kUInt64Descending:
      return CompareUsingLessThan(other.uint64_val_, uint64_val_);
    case KeyEntryType::kUInt64:
      return CompareUsingLessThan(uint64_val_, other.uint64_val_);
    case KeyEntryType::kInt64: FALLTHROUGH_INTENDED;
    case KeyEntryType::kArrayIndex:
      return CompareUsingLessThan(int64_val_, other.int64_val_);
    case KeyEntryType::kDoubleDescending:
      return CompareUsingLessThan(other.double_val_, double_val_);
    case KeyEntryType::kDouble:
      return CompareUsingLessThan(double_val_, other.double_val_);
    case KeyEntryType::kFloatDescending:
      return CompareUsingLessThan(other.float_val_, float_val_);
    case KeyEntryType::kFloat:
      return CompareUsingLessThan(float_val_, other.float_val_);
    case KeyEntryType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentType: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt16Hash:
      return CompareUsingLessThan(uint16_val_, other.uint16_val_);
    case KeyEntryType::kTimestampDescending:
      return CompareUsingLessThan(other.timestamp_val_, timestamp_val_);
    case KeyEntryType::kTimestamp:
      return CompareUsingLessThan(timestamp_val_, other.timestamp_val_);
    case KeyEntryType::kInetaddress:
      return CompareUsingLessThan(*inetaddress_val_, *(other.inetaddress_val_));
    case KeyEntryType::kInetaddressDescending:
      return CompareUsingLessThan(*(other.inetaddress_val_), *inetaddress_val_);
    case KeyEntryType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFrozen: {
      // Compare elements one by one.
      size_t min_size = std::min(frozen_val_->size(), other.frozen_val_->size());
      for (size_t i = 0; i < min_size; i++) {
        result = (*frozen_val_)[i].CompareTo((*other.frozen_val_)[i]);
        if (result != 0) {
          return result;
        }
      }

      // If elements are equal, compare lengths.
      if (type_ == KeyEntryType::kFrozenDescending) {
        return CompareUsingLessThan(other.frozen_val_->size(), frozen_val_->size());
      } else {
        return CompareUsingLessThan(frozen_val_->size(), other.frozen_val_->size());
      }
    }
    case KeyEntryType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case KeyEntryType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTableId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUuidDescending:
      return CompareUsingLessThan(other.uuid_val_, uuid_val_);
    case KeyEntryType::kUuid:
      return CompareUsingLessThan(uuid_val_, other.uuid_val_);
    case KeyEntryType::kColumnId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSystemColumnId:
      return CompareUsingLessThan(column_id_val_, other.column_id_val_);
    case KeyEntryType::kHybridTime:
      // HybridTimes are sorted in reverse order when wrapped in a PrimitiveValue.
      return -hybrid_time_val_.CompareTo(other.hybrid_time_val_);
    case KeyEntryType::kGinNull:
      return CompareUsingLessThan(gin_null_val_, other.gin_null_val_);
    IGNORE_SPECIAL_KEY_ENTRY_TYPES;
  }
  LOG(FATAL) << "Comparing invalid PrimitiveValues: " << *this << " and " << other;
}

void KeyEntryValue::ToQLValuePB(const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_value) const {
  if (SharedToQLValuePB(*this, ql_type, ql_value)) {
    return;
  }

  LOG(FATAL) << "Unsupported datatype " << ql_type->ToString();
}

const std::string& KeyEntryValue::GetString() const {
  DCHECK(IsString());
  return str_val_;
}

bool KeyEntryValue::IsString() const {
  return KeyEntryType::kString == type_ || KeyEntryType::kStringDescending == type_ ||
         KeyEntryType::kCollString == type_ || KeyEntryType::kCollStringDescending == type_;
}

bool KeyEntryValue::IsInt64() const {
  return KeyEntryType::kInt64 == type_ || KeyEntryType::kInt64Descending == type_;
}

int64_t KeyEntryValue::GetInt64() const {
  DCHECK(IsInt64());
  return int64_val_;
}

bool KeyEntryValue::IsFloat() const {
  return KeyEntryType::kFloat == type_ || KeyEntryType::kFloatDescending == type_;
}

float KeyEntryValue::GetFloat() const {
  DCHECK(IsFloat());
  return float_val_;
}

bool KeyEntryValue::IsDouble() const {
  return KeyEntryType::kDouble == type_ || KeyEntryType::kDoubleDescending == type_;
}

double KeyEntryValue::GetDouble() const {
  DCHECK(IsDouble());
  return double_val_;
}

bool KeyEntryValue::IsColumnId() const {
  return docdb::IsColumnId(type_);
}

ColumnId KeyEntryValue::GetColumnId() const {
  DCHECK(IsColumnId());
  return column_id_val_;
}

uint8_t KeyEntryValue::GetGinNull() const {
  DCHECK(type_ == KeyEntryType::kGinNull);
  return gin_null_val_;
}

bool KeyEntryValue::IsInt32() const {
  return KeyEntryType::kInt32 == type_ || KeyEntryType::kInt32Descending == type_;
}

bool KeyEntryValue::IsUInt16Hash() const {
  return type_ == KeyEntryType::kUInt16Hash;
}

uint16_t KeyEntryValue::GetUInt16Hash() const {
  DCHECK(IsUInt16Hash());
  return uint16_val_;
}

int32_t KeyEntryValue::GetInt32() const {
  DCHECK(IsInt32());
  return int32_val_;
}

bool KeyEntryValue::IsUInt32() const {
  return KeyEntryType::kUInt32 == type_ || KeyEntryType::kUInt32Descending == type_;
}

uint32_t KeyEntryValue::GetUInt32() const {
  DCHECK(IsUInt32());
  return uint32_val_;
}

bool KeyEntryValue::IsUInt64() const {
  return KeyEntryType::kUInt64 == type_ || KeyEntryType::kUInt64Descending == type_;
}

uint64_t KeyEntryValue::GetUInt64() const {
  DCHECK(IsUInt64());
  return uint64_val_;
}

bool KeyEntryValue::IsDecimal() const {
  return KeyEntryType::kDecimal == type_ || KeyEntryType::kDecimalDescending == type_;
}

const std::string& KeyEntryValue::GetDecimal() const {
  DCHECK(IsDecimal());
  return str_val_;
}

bool KeyEntryValue::IsVarInt() const {
  return KeyEntryType::kVarInt == type_ || KeyEntryType::kVarIntDescending == type_;
}

const std::string& KeyEntryValue::GetVarInt() const {
  DCHECK(IsVarInt());
  return str_val_;
}

bool KeyEntryValue::IsTimestamp() const {
  return KeyEntryType::kTimestamp == type_ || KeyEntryType::kTimestampDescending == type_;
}

Timestamp KeyEntryValue::GetTimestamp() const {
  DCHECK(IsTimestamp());
  return timestamp_val_;
}

bool operator==(const KeyEntryValue& lhs, const KeyEntryValue& rhs) {
  if (lhs.type_ != rhs.type_) {
    return false;
  }
  switch (lhs.type_) {
    CASE_EMPTY_KEY_ENTRY_TYPES
        return true;

    case KeyEntryType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kCollString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimal: FALLTHROUGH_INTENDED;
    case KeyEntryType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kVarInt:
        return lhs.str_val_ == rhs.str_val_;

    case KeyEntryType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFrozen:
        return *lhs.frozen_val_ == *rhs.frozen_val_;

    case KeyEntryType::kInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt32:
        return lhs.int32_val_ == rhs.int32_val_;

    case KeyEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32:
        return lhs.uint32_val_ == rhs.uint32_val_;

    case KeyEntryType::kUInt64Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt64:
        return lhs.uint64_val_ == rhs.uint64_val_;

    case KeyEntryType::kInt64Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt64: FALLTHROUGH_INTENDED;
    case KeyEntryType::kArrayIndex:
        return lhs.int64_val_ == rhs.int64_val_;

    case KeyEntryType::kFloatDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFloat: {
      if (util::IsNanFloat(lhs.float_val_) && util::IsNanFloat(rhs.float_val_)) {
        return true;
      }
      return lhs.float_val_ == rhs.float_val_;
    }
    case KeyEntryType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDouble: {
      if (util::IsNanDouble(lhs.double_val_) && util::IsNanDouble(rhs.double_val_)) {
        return true;
      }
      return lhs.double_val_ == rhs.double_val_;
    }
    case KeyEntryType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case KeyEntryType::kObsoleteIntentType: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt16Hash:
        return lhs.uint16_val_ == rhs.uint16_val_;

    case KeyEntryType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTimestamp:
        return lhs.timestamp_val_ == rhs.timestamp_val_;
    case KeyEntryType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInetaddress:
        return *lhs.inetaddress_val_ == *rhs.inetaddress_val_;
    case KeyEntryType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case KeyEntryType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTableId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUuidDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUuid:
        return lhs.uuid_val_ == rhs.uuid_val_;

    case KeyEntryType::kColumnId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSystemColumnId:
        return lhs.column_id_val_ == rhs.column_id_val_;
    case KeyEntryType::kHybridTime:
        return lhs.hybrid_time_val_.CompareTo(rhs.hybrid_time_val_) == 0;
    case KeyEntryType::kGinNull:
        return lhs.gin_null_val_ == rhs.gin_null_val_;

    IGNORE_SPECIAL_KEY_ENTRY_TYPES;
  }
  FATAL_INVALID_ENUM_VALUE(KeyEntryType, lhs.type_);
}

}  // namespace docdb
}  // namespace yb
