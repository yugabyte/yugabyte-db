// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_PRIMITIVE_VALUE_H_
#define YB_DOCDB_PRIMITIVE_VALUE_H_

#include <memory.h>

#include <string>
#include <vector>
#include <ostream>

#include "yb/docdb/value_type.h"
#include "yb/docdb/key_bytes.h"
#include "yb/common/timestamp.h"

namespace yb {
namespace docdb {

template<typename T>
int GenericCompare(const T& a, const T& b) {
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

class PrimitiveValue {
 public:
  explicit PrimitiveValue() : type_(ValueType::kNull) {
  }

  PrimitiveValue(const PrimitiveValue& other) {
    if (other.type_ == ValueType::kString) {
      type_ = other.type_;
      new(&str_val_) std::string(other.str_val_);
    } else {
      memcpy(this, &other, sizeof(PrimitiveValue));
    }
  };

  PrimitiveValue& operator=(const PrimitiveValue& other) {
    this->~PrimitiveValue();
    new(this) PrimitiveValue(other);
    return *this;
  }

  explicit PrimitiveValue(const std::string& s) : type_(ValueType::kString) {
    new(&str_val_) std::string(s);
  }

  explicit PrimitiveValue(const char* s) : type_(ValueType::kString) {
    new(&str_val_) std::string(s);
  }

  explicit PrimitiveValue(int64_t v)
      : type_(ValueType::kInt64) {
    // Avoid using an initializer for a union field (got surprising and unexpected results with
    // that approach). Use a direct assignment instead.
    int64_val_ = v;
  }

  explicit PrimitiveValue(const Timestamp& timestamp)
      : type_(ValueType::kTimestamp) {
    timestamp_val_ = timestamp;
  }

  ValueType value_type() const { return type_; }

  void AppendToKey(KeyBytes* key_bytes) const;

  std::string ToValue() const;

  // Convert this value to a human-readable string for logging / debugging.
  std::string ToString() const;

  ~PrimitiveValue() {
    if (type_ == ValueType::kString) {
      str_val_.~basic_string();
    }
    // Timestamp does not need its destructor to be called, because it is a simple wrapper over an
    // unsigned 64-bit integer.
  }

  // Decodes a primitive value from the given slice representing a RocksDB key in our key encoding
  // format and consumes a prefix of the slice.
  Status DecodeFromKey(rocksdb::Slice* slice);

  // Decodes a primitive value from the given slice representing a RocksDB value in our value
  // encoding format. Expects the entire slice to be consumed and returns an error otherwise.
  Status DecodeFromValue(const rocksdb::Slice& rocksdb_value);

  static PrimitiveValue Double(double d);
  static PrimitiveValue ArrayIndex(int64_t index);
  static PrimitiveValue UInt32Hash(uint32_t hash);

  KeyBytes ToKeyBytes() const;

  Timestamp timestamp() {
    DCHECK(type_ == ValueType::kTimestamp);
    return timestamp_val_;
  }

  // As strange as it may sound, an instance of this class may sometimes contain a single byte that
  // indicates an empty data structure of a certain type (object, array), or a tombstone. This
  // method can tell whether what's stored here is an actual primitive value.
  bool IsPrimitive() {
    return IsPrimitiveValueType(type_);
  }

  int CompareTo(const PrimitiveValue& other) const;

  static const PrimitiveValue kNull;
  static const PrimitiveValue kTrue;
  static const PrimitiveValue kFalse;
  static const PrimitiveValue kTombstone;

 private:

  static PrimitiveValue FromValueType(ValueType kValueType);

  // TODO: make this a method.
  friend bool operator== (const PrimitiveValue& a, const PrimitiveValue& b);

  ValueType type_;
  union {
    int64_t int64_val_;
    uint32_t uint32_val_;
    Timestamp timestamp_val_;
    std::string str_val_;
    double double_val_;
  };
};

inline std::ostream& operator<< (std::ostream& out, const PrimitiveValue& primitive_value) {
  out << primitive_value.ToString();
  return out;
}

bool operator== (const PrimitiveValue& a, const PrimitiveValue& b);

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

// Timestamps are converted to strings as <this_string_constant>(<timestamp_value>).
extern const char* const kTimestampConstantPrefixStr;

// Convert the given timestamp to string, putting it in parentheses prefixed by
// kTimestampConstantPrefixStr.
std::string TimestampToPrefixedStr(Timestamp ts);

}
}

#endif
