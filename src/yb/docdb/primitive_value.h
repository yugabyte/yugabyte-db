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

#ifndef YB_DOCDB_PRIMITIVE_VALUE_H_
#define YB_DOCDB_PRIMITIVE_VALUE_H_

#include <memory.h>

#include <string>
#include <vector>
#include <ostream>

#include "yb/util/slice.h"

#include "yb/common/common.pb.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/docdb/key_bytes.h"
#include "yb/docdb/value_type.h"
#include "yb/util/decimal.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace docdb {

// A necessary use of a forward declaration to avoid circular inclusion.
class SubDocument;

enum class SystemColumnIds : ColumnIdRep {
  kLivenessColumn = 0  // Stores the TTL for QL rows inserted using an INSERT statement.
};

enum class SortOrder : int8_t {
  kAscending = 0,
  kDescending
};

class PrimitiveValue {
 public:
  PrimitiveValue() : type_(ValueType::kNull) {
  }

  explicit PrimitiveValue(ValueType value_type);

  PrimitiveValue(const PrimitiveValue& other) {
    if (other.type_ == ValueType::kString || other.type_ == ValueType::kStringDescending) {
      type_ = other.type_;
      new(&str_val_) std::string(other.str_val_);
    } else if (other.type_ == ValueType::kInetaddress
        || other.type_ == ValueType::kInetaddressDescending) {
      type_ = other.type_;
      inetaddress_val_ = new InetAddress(*(other.inetaddress_val_));
    } else if (other.type_ == ValueType::kDecimal || other.type_ == ValueType::kDecimalDescending) {
      type_ = other.type_;
      new(&decimal_val_) std::string(other.decimal_val_);
    } else if (other.type_ == ValueType::kUuid || other.type_ == ValueType::kUuidDescending) {
      type_ = other.type_;
      new(&uuid_val_) Uuid(std::move((other.uuid_val_)));
    } else {
      memmove(this, &other, sizeof(PrimitiveValue));
    }
  }

  PrimitiveValue(PrimitiveValue&& other) {
    MoveFrom(&other);
  }

  PrimitiveValue& operator =(const PrimitiveValue& other) {
    this->~PrimitiveValue();
    new(this) PrimitiveValue(other);
    return *this;
  }

  PrimitiveValue& operator =(PrimitiveValue&& other) {
    this->~PrimitiveValue();
    MoveFrom(&other);
    return *this;
  }

  explicit PrimitiveValue(const std::string& s, SortOrder sort_order = SortOrder::kAscending) {
    if (sort_order == SortOrder::kDescending) {
      type_ = ValueType::kStringDescending;
    } else {
      type_ = ValueType::kString;
    }
    new(&str_val_) std::string(s);
  }

  explicit PrimitiveValue(const char* s, SortOrder sort_order = SortOrder::kAscending) {
    if (sort_order == SortOrder::kDescending) {
      type_ = ValueType::kStringDescending;
    } else {
      type_ = ValueType::kString;
    }
    new(&str_val_) std::string(s);
  }

  explicit PrimitiveValue(int64_t v, SortOrder sort_order = SortOrder::kAscending) {
    if (sort_order == SortOrder::kDescending) {
      type_ = ValueType::kInt64Descending;
    } else {
      type_ = ValueType::kInt64;
    }
    // Avoid using an initializer for a union field (got surprising and unexpected results with
    // that approach). Use a direct assignment instead.
    int64_val_ = v;
  }

  explicit PrimitiveValue(const Timestamp& timestamp,
                          SortOrder sort_order = SortOrder::kAscending) {
    if (sort_order == SortOrder::kDescending) {
      type_ = ValueType::kTimestampDescending;
    } else {
      type_ = ValueType::kTimestamp;
    }
    timestamp_val_ = timestamp;
  }

  explicit PrimitiveValue(const InetAddress& inetaddress,
                          SortOrder sort_order = SortOrder::kAscending) {
    if (sort_order == SortOrder::kDescending) {
      type_ = ValueType::kInetaddressDescending;
    } else {
      type_ = ValueType::kInetaddress;
    }
    inetaddress_val_ = new InetAddress(inetaddress);
  }

  explicit PrimitiveValue(const Uuid& uuid,
                          SortOrder sort_order = SortOrder::kAscending) {
    if (sort_order == SortOrder::kDescending) {
      type_ = ValueType::kUuidDescending;
    } else {
      type_ = ValueType::kUuid;
    }
    uuid_val_ = uuid;
  }

  explicit PrimitiveValue(const HybridTime& hybrid_time) : type_(ValueType::kHybridTime) {
    hybrid_time_val_ = DocHybridTime(hybrid_time);
  }

  explicit PrimitiveValue(const DocHybridTime& hybrid_time)
      : type_(ValueType::kHybridTime),
        hybrid_time_val_(hybrid_time) {
  }

  explicit PrimitiveValue(const ColumnId column_id) : type_(ValueType::kColumnId) {
    column_id_val_ = column_id;
  }

  // Converts a ColumnSchema::SortingType to its SortOrder equivalent.
  // ColumnSchema::SortingType::kAscending and ColumnSchema::SortingType::kNotSpecified get
  // converted to SortOrder::kAscending.
  // ColumnSchema::SortingType::kDescending gets converted to SortOrder::kDescending.
  static SortOrder SortOrderFromColumnSchemaSortingType(ColumnSchema::SortingType sorting_type);

  // Construct a primitive value from a Slice containing a Kudu value.
  static PrimitiveValue FromKuduValue(DataType data_type, Slice slice);

  // Construct a primitive value from a QLValuePB.
  static PrimitiveValue FromQLValuePB(const QLValuePB& value,
                                       ColumnSchema::SortingType sorting_type);

  // Set a primitive value in a QLValuePB.
  static void ToQLValuePB(const PrimitiveValue& pv,
                           const std::shared_ptr<QLType>& ql_type,
                           QLValuePB* ql_val);

  // Construct a primitive value from a QLExpressionPB.
  static PrimitiveValue FromQLExpressionPB(const QLExpressionPB& ql_expr,
                                            ColumnSchema::SortingType sorting_type);

  // Set a primitive value in a QLExpressionPB.
  static void ToQLExpressionPB(const PrimitiveValue& pv,
                                const std::shared_ptr<QLType>& ql_type,
                                QLExpressionPB* ql_expr);

  ValueType value_type() const { return type_; }

  void AppendToKey(KeyBytes* key_bytes) const;

  std::string ToValue() const;

  // Convert this value to a human-readable string for logging / debugging.
  std::string ToString() const;

  ~PrimitiveValue() {
    if (type_ == ValueType::kString || type_ == ValueType::kStringDescending) {
      str_val_.~basic_string();
    } else if (type_ == ValueType::kInetaddress || type_ == ValueType::kInetaddressDescending) {
      delete inetaddress_val_;
    } else if (type_ == ValueType::kDecimal || type_ == ValueType::kDecimalDescending) {
      decimal_val_.~basic_string();
    }
    // HybridTime does not need its destructor to be called, because it is a simple wrapper over an
    // unsigned 64-bit integer.
  }

  // Decodes a primitive value from the given slice representing a RocksDB key in our key encoding
  // format and consumes a prefix of the slice.
  static CHECKED_STATUS DecodeKey(rocksdb::Slice* slice, PrimitiveValue* out);
  CHECKED_STATUS DecodeFromKey(rocksdb::Slice* slice);

  // Decodes a primitive value from the given slice representing a RocksDB value in our value
  // encoding format. Expects the entire slice to be consumed and returns an error otherwise.
  CHECKED_STATUS DecodeFromValue(const rocksdb::Slice& rocksdb_slice);

  static PrimitiveValue Double(double d);
  static PrimitiveValue Float(float f);
  // decimal_str represents a human readable string representing the decimal number, e.g. "0.03".
  static PrimitiveValue Decimal(const std::string& decimal_str, SortOrder sort_order);
  static PrimitiveValue ArrayIndex(int64_t index);
  static PrimitiveValue UInt16Hash(uint16_t hash);
  static PrimitiveValue SystemColumnId(ColumnId column_id);
  static PrimitiveValue SystemColumnId(SystemColumnIds system_column_id);
  static PrimitiveValue Int32(int32_t v, SortOrder sort_order = SortOrder::kAscending);
  static PrimitiveValue TransactionId(Uuid transaction_id);
  static PrimitiveValue IntentTypeValue(IntentType intent_type);

  KeyBytes ToKeyBytes() const;

  DocHybridTime hybrid_time() const {
    DCHECK(type_ == ValueType::kHybridTime);
    return hybrid_time_val_;
  }

  // As strange as it may sound, an instance of this class may sometimes contain a single byte that
  // indicates an empty data structure of a certain type (object, array), or a tombstone. This
  // method can tell whether what's stored here is an actual primitive value.
  bool IsPrimitive() const {
    return IsPrimitiveValueType(type_);
  }

  int CompareTo(const PrimitiveValue& other) const;

  // Assuming this PrimitiveValue represents a string, return a Slice pointing to it.
  // This returns a YB slice, not a RocksDB slice, based on what was needed when this function was
  // implemented. This distinction should go away if we merge RocksDB and YB Slice classes.
  Slice GetStringAsSlice() const {
    DCHECK(ValueType::kString == type_ || ValueType::kStringDescending == type_);
    return Slice(str_val_);
  }

  const std::string& GetString() const {
    DCHECK(ValueType::kString == type_ || ValueType::kStringDescending == type_);
    return str_val_;
  }

  int32_t GetInt32() const {
    DCHECK(ValueType::kInt32 == type_ || ValueType::kInt32Descending == type_);
    return int32_val_;
  }

  int64_t GetInt64() const {
    DCHECK(ValueType::kInt64 == type_ || ValueType::kInt64Descending == type_);
    return int64_val_;
  }

  uint16_t GetUInt16() const {
    DCHECK(ValueType::kUInt16Hash == type_ || ValueType::kIntentType == type_);
    return uint16_val_;
  }

  double GetDouble() const {
    DCHECK_EQ(ValueType::kDouble, type_);
    return double_val_;
  }

  float GetFloat() const {
    DCHECK_EQ(ValueType::kFloat, type_);
    return float_val_;
  }

  const std::string& GetDecimal() const {
    DCHECK(ValueType::kDecimal == type_ || ValueType::kDecimalDescending == type_);
    return decimal_val_;
  }

  Timestamp GetTimestamp() const {
    DCHECK(ValueType::kTimestamp == type_ || ValueType::kTimestampDescending == type_);
    return timestamp_val_;
  }

  const InetAddress* GetInetaddress() const {
    DCHECK(type_ == ValueType::kInetaddress || type_ == ValueType::kInetaddressDescending);
    return inetaddress_val_;
  }

  const Uuid& GetUuid() const {
    DCHECK(type_ == ValueType::kUuid || type_ == ValueType::kUuidDescending ||
        type_ == ValueType::kTransactionId);
    return uuid_val_;
  }

  ColumnId GetColumnId() const {
    DCHECK(type_ == ValueType::kColumnId || type_ == ValueType::kSystemColumnId);
    return column_id_val_;
  }

  bool operator <(const PrimitiveValue& other) const {
    return CompareTo(other) < 0;
  }

  bool operator >(const PrimitiveValue& other) const {
    return CompareTo(other) > 0;
  }

  bool operator==(const PrimitiveValue& other) const;

  bool operator!=(const PrimitiveValue& other) const { return !(*this == other); }

  int64_t GetTtl() const {
    return ttl_seconds_;
  }

  int64_t GetWritetime() const {
    return write_time_;
  }

  void SetTtl(const int64_t ttl_seconds) {
    ttl_seconds_ = ttl_seconds;
  }

  void SetWritetime(const int64_t write_time) {
    write_time_ = write_time;
  }

 protected:

  // Column attributes
  int64_t ttl_seconds_;
  int64_t write_time_;

  ValueType type_;

  // TODO: do we have to worry about alignment here?
  union {
    int32_t int32_val_;
    int64_t int64_val_;
    uint16_t uint16_val_;
    DocHybridTime hybrid_time_val_;
    std::string str_val_;
    float float_val_;
    double double_val_;
    Timestamp timestamp_val_;
    InetAddress* inetaddress_val_;
    Uuid uuid_val_;
    // This is used in SubDocument to hold a pointer to a map or a vector.
    void* complex_data_structure_;
    ColumnId column_id_val_;
    std::string decimal_val_;
  };

 private:

  // This is used in both the move constructor and the move assignment operator. Assumes this object
  // has not been constructed, or that the destructor has just been called.
  void MoveFrom(PrimitiveValue* other) {
    if (this == other) {
      return;
    }

    if (other->type_ == ValueType::kString || other->type_ == ValueType::kStringDescending) {
      type_ = other->type_;
      new(&str_val_) std::string(std::move(other->str_val_));
      // The moved-from object should now be in a "valid but unspecified" state as per the standard.
    } else if (other->type_ == ValueType::kInetaddress
        || other->type_ == ValueType::kInetaddressDescending) {
      type_ = other->type_;
      inetaddress_val_ = new InetAddress(std::move(*(other->inetaddress_val_)));
    } else if (other->type_ == ValueType::kDecimal ||
               other->type_ == ValueType::kDecimalDescending) {
      type_ = other->type_;
      new(&decimal_val_) std::string(std::move(other->decimal_val_));
    } else if (other->type_ == ValueType::kUuid || other->type_ == ValueType::kUuidDescending) {
      type_ = other->type_;
      new(&uuid_val_) Uuid(std::move((other->uuid_val_)));
    } else {
      // Non-string primitive values only have plain old data. We are assuming there is no overlap
      // between the two objects, so we're using memcpy instead of memmove.
      memcpy(this, other, sizeof(PrimitiveValue));
#ifndef NDEBUG
      // We could just leave the old object as is for it to be in a "valid but unspecified" state.
      // However, in debug mode we clear the old object's state to make sure we don't attempt to use
      // it.
      memset(other, 0xab, sizeof(PrimitiveValue));
      // Restore the type. There should be no deallocation for non-string types anyway.
      other->type_ = ValueType::kNull;
#endif
    }
  }
};

inline std::ostream& operator<<(std::ostream& out, const PrimitiveValue& primitive_value) {
  out << primitive_value.ToString();
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const SortOrder sort_order) {
  string sort_order_name = sort_order == SortOrder::kAscending ? "kAscending" : "kDescending";
  out << sort_order_name;
  return out;
}

// A variadic template utility for creating vectors with PrimitiveValue elements out of arbitrary
// sequences of arguments of supported types.
inline void AppendPrimitiveValues(std::vector<PrimitiveValue>* dest) {}

template <class T, class ...U>
inline void AppendPrimitiveValues(std::vector<PrimitiveValue>* dest,
                                  T first_arg,
                                  U... more_args) {
  dest->push_back(PrimitiveValue(first_arg));
  AppendPrimitiveValues(dest, more_args...);
}

template <class ...T>
inline std::vector<PrimitiveValue> PrimitiveValues(T... args) {
  std::vector<PrimitiveValue> v;
  AppendPrimitiveValues(&v, args...);
  return v;
}

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_PRIMITIVE_VALUE_H_
