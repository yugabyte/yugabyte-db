// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_PRIMITIVE_VALUE_H_
#define YB_DOCDB_PRIMITIVE_VALUE_H_

#include <memory.h>

#include <string>
#include <vector>
#include <ostream>

#include "rocksdb/slice.h"

#include "yb/docdb/value_type.h"
#include "yb/docdb/key_bytes.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/common.pb.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"

namespace yb {
namespace docdb {

// A necessary use of a forward declaration to avoid circular inclusion.
class SubDocument;

template<typename T>
int GenericCompare(const T& a, const T& b) {
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

class PrimitiveValue {
 public:
  PrimitiveValue() : type_(ValueType::kNull) {
  }

  explicit PrimitiveValue(ValueType value_type);

  PrimitiveValue(const PrimitiveValue& other) {
    if (other.type_ == ValueType::kString) {
      type_ = other.type_;
      new(&str_val_) std::string(other.str_val_);
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

  explicit PrimitiveValue(const std::string& s) : type_(ValueType::kString) {
    new(&str_val_) std::string(s);
  }

  explicit PrimitiveValue(const char* s) : type_(ValueType::kString) {
    new(&str_val_) std::string(s);
  }

  explicit PrimitiveValue(int64_t v) : type_(ValueType::kInt64) {
    // Avoid using an initializer for a union field (got surprising and unexpected results with
    // that approach). Use a direct assignment instead.
    int64_val_ = v;
  }

  explicit PrimitiveValue(const HybridTime& hybrid_time) : type_(ValueType::kHybridTime) {
    hybrid_time_val_ = hybrid_time;
  }

  // Construct a primitive value from a Slice containing a Kudu value.
  static PrimitiveValue FromKuduValue(DataType data_type, Slice slice);

  // Construct a primitive value from a YQLValuePB.
  static PrimitiveValue FromYQLValuePB(const YQLValuePB& value);

  // Construct a primitive value from a YQLValue.
  static PrimitiveValue FromYQLValue(const YQLValue& value);

  // Set a primitive value in a YQLValue.
  void ToYQLValue(YQLValue* v) const;

  ValueType value_type() const { return type_; }

  void AppendToKey(KeyBytes* key_bytes) const;

  std::string ToValue() const;

  // Convert this value to a human-readable string for logging / debugging.
  std::string ToString() const;

  ~PrimitiveValue() {
    if (type_ == ValueType::kString) {
      str_val_.~basic_string();
    }
    // HybridTime does not need its destructor to be called, because it is a simple wrapper over an
    // unsigned 64-bit integer.
  }

  // Decodes a primitive value from the given slice representing a RocksDB key in our key encoding
  // format and consumes a prefix of the slice.
  CHECKED_STATUS DecodeFromKey(rocksdb::Slice* slice);

  // Decodes a primitive value from the given slice representing a RocksDB value in our value
  // encoding format. Expects the entire slice to be consumed and returns an error otherwise.
  CHECKED_STATUS DecodeFromValue(const rocksdb::Slice& rocksdb_slice);

  static PrimitiveValue Double(double d);
  static PrimitiveValue ArrayIndex(int64_t index);
  static PrimitiveValue UInt32Hash(uint32_t hash);

  KeyBytes ToKeyBytes() const;

  HybridTime hybrid_time() const {
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
    DCHECK_EQ(ValueType::kString, type_);
    return Slice(str_val_);
  }

  const std::string& GetString() const {
    DCHECK_EQ(ValueType::kString, type_);
    return str_val_;
  }

  void SwapStringValue(std::string *other) {
    DCHECK_EQ(ValueType::kString, type_);
    str_val_.swap(*other);
  }

  int64_t GetInt64() const {
    DCHECK_EQ(ValueType::kInt64, type_);
    return int64_val_;
  }

  double GetDouble() const {
    DCHECK_EQ(ValueType::kDouble, type_);
    return double_val_;
  }

  bool operator <(const PrimitiveValue& other) const {
    return CompareTo(other) < 0;
  }

  bool operator >(const PrimitiveValue& other) const {
    return CompareTo(other) > 0;
  }

  bool operator==(const PrimitiveValue& other) const;

  bool operator!=(const PrimitiveValue& other) const { return !(*this == other); }

 protected:

  ValueType type_;

  // TOOD: do we have to worry about alignment here?
  union {
    int64_t int64_val_;
    uint32_t uint32_val_;
    HybridTime hybrid_time_val_;
    std::string str_val_;
    double double_val_;
    // This is used in SubDocument to hold a pointer to a map or a vector.
    void* complex_data_structure_;
  };

 private:

  // This is used in both the move constructor and the move assignment operator. Assumes this object
  // has not been constructed, or that the destructor has just been called.
  void MoveFrom(PrimitiveValue* other) {
    if (this == other) {
      return;
    }

    if (other->type_ == ValueType::kString) {
      type_ = other->type_;
      new(&str_val_) std::string(std::move(other->str_val_));
      // The moved-from object should now be in a "valid but unspecified" state as per the standard.
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
      other->type_ = type_;
#endif
    }
  }
};

inline std::ostream& operator<<(std::ostream& out, const PrimitiveValue& primitive_value) {
  out << primitive_value.ToString();
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
