// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#pragma once

#include <stdint.h>

#include <string>
#include <type_traits>

#include "yb/util/logging.h"

#include "yb/common/common_fwd.h"
#include "yb/common/value.messages.h"

#include "yb/gutil/mathlimits.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/numbers.h"

#include "yb/util/slice.h"

namespace yb {

// The size of the in-memory format of the largest
// type we support.
const int kLargestTypeSize = sizeof(Slice);

class TypeInfo;

// This is the important bit of this header:
// given a type enum, get the TypeInfo about it.
extern const TypeInfo* GetTypeInfo(DataType type);

// Information about a given type.
// This is a runtime equivalent of the TypeTraits template below.
class TypeInfo {
 public:
  using AppendDebugFunc = void (*)(const void*, std::string*);
  using CompareFunc = int (*)(const void*, const void*);

  DataType type;
  DataType physical_type;
  std::string name;
  size_t size;
  const void* min_value;
  AppendDebugFunc append_func;
  CompareFunc compare_func;

  bool var_length() const {
    return physical_type == DataType::BINARY;
  }

  int Compare(const void* lhs, const void* rhs) const {
    return compare_func(lhs, rhs);
  }

  void AppendDebugStringForValue(const void* value, std::string* out) const {
    append_func(value, out);
  }

  void CopyMinValue(void* dst) const {
    memcpy(dst, min_value, size);
  }

  bool is_collection() const;
};

template<DataType Type> struct DataTypeTraits {};

template<DataType Type>
static int GenericCompare(const void *lhs, const void *rhs) {
  typedef typename DataTypeTraits<Type>::cpp_type CppType;
  CppType lhs_int = *reinterpret_cast<const CppType *>(lhs);
  CppType rhs_int = *reinterpret_cast<const CppType *>(rhs);
  if (lhs_int < rhs_int) {
    return -1;
  } else if (lhs_int > rhs_int) {
    return 1;
  } else {
    return 0;
  }
}

template<>
struct DataTypeTraits<DataType::UINT8> {
  static const DataType physical_type = DataType::UINT8;
  typedef uint8_t cpp_type;
  static const char *name() {
    return "uint8";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const uint8_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::UINT8>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::INT8> {
  static const DataType physical_type = DataType::INT8;
  typedef int8_t cpp_type;
  static const char *name() {
    return "int8";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const int8_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::INT8>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::UINT16> {
  static const DataType physical_type = DataType::UINT16;
  typedef uint16_t cpp_type;
  static const char *name() {
    return "uint16";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const uint16_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::UINT16>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::INT16> {
  static const DataType physical_type = DataType::INT16;
  typedef int16_t cpp_type;
  static const char *name() {
    return "int16";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const int16_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::INT16>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::UINT32> {
  static const DataType physical_type = DataType::UINT32;
  typedef uint32_t cpp_type;
  static const char *name() {
    return "uint32";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const uint32_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::UINT32>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::INT32> {
  static const DataType physical_type = DataType::INT32;
  typedef int32_t cpp_type;
  static const char *name() {
    return "int32";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const int32_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::INT32>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::UINT64> {
  static const DataType physical_type = DataType::UINT64;
  typedef uint64_t cpp_type;
  static const char *name() {
    return "uint64";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const uint64_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::UINT64>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::INT64> {
  static const DataType physical_type = DataType::INT64;
  typedef int64_t cpp_type;
  static const char *name() {
    return "int64";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleItoa(*reinterpret_cast<const int64_t *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::INT64>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::FLOAT> {
  static const DataType physical_type = DataType::FLOAT;
  typedef float cpp_type;
  static const char *name() {
    return "float";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleFtoa(*reinterpret_cast<const float *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::FLOAT>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::DOUBLE> {
  static const DataType physical_type = DataType::DOUBLE;
  typedef double cpp_type;
  static const char *name() {
    return "double";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    str->append(SimpleDtoa(*reinterpret_cast<const double *>(val)));
  }
  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::DOUBLE>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    return &MathLimits<cpp_type>::kMin;
  }
};

template<>
struct DataTypeTraits<DataType::BINARY> {
  static const DataType physical_type = DataType::BINARY;
  typedef Slice cpp_type;
  static const char *name() {
    return "binary";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    const Slice *s = reinterpret_cast<const Slice *>(val);
    str->append(strings::CHexEscape(s->ToString()));
  }

  static int Compare(const void *lhs, const void *rhs) {
    const Slice *lhs_slice = reinterpret_cast<const Slice *>(lhs);
    const Slice *rhs_slice = reinterpret_cast<const Slice *>(rhs);
    return lhs_slice->compare(*rhs_slice);
  }
  static const cpp_type* min_value() {
    static Slice s("");
    return &s;
  }
};

template<>
struct DataTypeTraits<DataType::BOOL> {
  static const DataType physical_type = DataType::BOOL;
  typedef bool cpp_type;
  static const char* name() {
    return "bool";
  }
  static void AppendDebugStringForValue(const void* val, std::string* str) {
    str->append(*reinterpret_cast<const bool *>(val) ? "true" : "false");
  }

  static int Compare(const void *lhs, const void *rhs) {
    return GenericCompare<DataType::BOOL>(lhs, rhs);
  }
  static const cpp_type* min_value() {
    static bool b = false;
    return &b;
  }
};

// Base class for types that are derived, that is that have some other type as the
// physical representation.
template<DataType PhysicalType>
struct DerivedTypeTraits {
  typedef typename DataTypeTraits<PhysicalType>::cpp_type cpp_type;
  static const DataType physical_type = PhysicalType;

  static void AppendDebugStringForValue(const void *val, std::string *str) {
    DataTypeTraits<PhysicalType>::AppendDebugStringForValue(val, str);
  }

  static int Compare(const void *lhs, const void *rhs) {
    return DataTypeTraits<PhysicalType>::Compare(lhs, rhs);
  }

  static const cpp_type* min_value() {
    return DataTypeTraits<PhysicalType>::min_value();
  }
};

template<>
struct DataTypeTraits<DataType::STRING> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "string";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    const Slice *s = reinterpret_cast<const Slice *>(val);
    str->append(strings::Utf8SafeCEscape(s->ToString()));
  }
};

template<>
struct DataTypeTraits<DataType::INET> : public DerivedTypeTraits<DataType::BINARY> {
  static const char* name() {
    return "inet";
  }

  static void AppendDebugStringForValue(const void *val, std::string *str);
};

template<>
struct DataTypeTraits<DataType::JSONB> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "jsonb";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str) {
    const Slice *s = reinterpret_cast<const Slice *>(val);
    str->append(strings::Utf8SafeCEscape(s->ToString()));
  }
};

template<>
struct DataTypeTraits<DataType::UUID> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "uuid";
  }
  static void AppendDebugStringForValue(const void *val, std::string *str);
};

template<>
struct DataTypeTraits<DataType::TIMEUUID> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "timeuuid";
  }

  static void AppendDebugStringForValue(const void *val, std::string *str);
};

template<>
struct DataTypeTraits<DataType::MAP> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "map";
  }

  // using the default implementation inherited from BINARY for AppendDebugStringForValue
  // TODO much of this codepath should be retired and we should systematically use QLValue instead
  // of Kudu Slice [ENG-1235]
};

template<>
struct DataTypeTraits<DataType::SET> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "set";
  }

  // using the default implementation inherited from BINARY for AppendDebugStringForValue
  // TODO much of this codepath should be retired and we should systematically use QLValue instead
  // of Kudu Slice [ENG-1235]
};

template<>
struct DataTypeTraits<DataType::LIST> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "list";
  }

  // using the default implementation inherited from BINARY for AppendDebugStringForValue
  // TODO much of this codepath should be retired and we should systematically use QLValue instead
  // of Kudu Slice [ENG-1235]
};

template<>
struct DataTypeTraits<DataType::USER_DEFINED_TYPE> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "user_defined_type";
  }

  // using the default implementation inherited from BINARY for AppendDebugStringForValue
  // TODO much of this codepath should be retired and we should systematically use QLValue instead
  // of Kudu Slice [ENG-1235]
};

template<>
struct DataTypeTraits<DataType::FROZEN> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "frozen";
  }

  // using the default implementation inherited from BINARY for AppendDebugStringForValue
  // TODO much of this codepath should be retired and we should systematically use QLValue instead
  // of Kudu Slice [ENG-1235]
};

template <>
struct DataTypeTraits<DataType::TUPLE> : public DerivedTypeTraits<DataType::BINARY> {
  static const char *name() { return "tuple"; }

  // using the default implementation inherited from BINARY for AppendDebugStringForValue
  // TODO much of this codepath should be retired and we should systematically use QLValue instead
  // of Kudu Slice [ENG-1235]
};

template<>
struct DataTypeTraits<DataType::DECIMAL> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "decimal";
  }
  static void AppendDebugDecimalForValue(const void* val, std::string* str) {
    const Slice *s = reinterpret_cast<const Slice *>(val);
    str->append(strings::Utf8SafeCEscape(s->ToString()));
  }
};

template<>
struct DataTypeTraits<DataType::VARINT> : public DerivedTypeTraits<DataType::BINARY>{
  static const char* name() {
    return "varint";
  }
  static void AppendDebugVarIntForValue(const void* val, std::string* str) {
    const Slice *s = reinterpret_cast<const Slice *>(val);
    str->append(strings::Utf8SafeCEscape(s->ToString()));
  }
};


static const char* kDateFormat = "%Y-%m-%d %H:%M:%S";
static const char* kDateMicrosAndTzFormat = "%s.%06d GMT";

template<>
struct DataTypeTraits<DataType::TIMESTAMP> : public DerivedTypeTraits<DataType::INT64>{
  static const int US_TO_S = 1000L * 1000L;

  static const char* name() {
    return "timestamp";
  }

  static void AppendDebugStringForValue(const void* val, std::string* str) {
    int64_t timestamp_micros = *reinterpret_cast<const int64_t *>(val);
    time_t secs_since_epoch = timestamp_micros / US_TO_S;
    // If the time is negative we need to take into account that any microseconds
    // will actually decrease the time in seconds by one.
    int remaining_micros = timestamp_micros % US_TO_S;
    if (remaining_micros < 0) {
      secs_since_epoch--;
      remaining_micros = US_TO_S - std::abs(remaining_micros);
    }
    struct tm tm_info;
    gmtime_r(&secs_since_epoch, &tm_info);
    char time_up_to_secs[24];
    strftime(time_up_to_secs, sizeof(time_up_to_secs), kDateFormat, &tm_info);
    char time[40];
    snprintf(time, sizeof(time), kDateMicrosAndTzFormat, time_up_to_secs, remaining_micros);
    str->append(time);
  }
};

template<>
struct DataTypeTraits<DataType::DATE> : public DerivedTypeTraits<DataType::UINT32>{
  static const char* name() {
    return "date";
  }
};

template<>
struct DataTypeTraits<DataType::TIME> : public DerivedTypeTraits<DataType::INT64>{
  static const char* name() {
    return "time";
  }
};

// Instantiate this template to get static access to the type traits.
template<DataType datatype>
struct TypeTraits : public DataTypeTraits<datatype> {
  typedef typename DataTypeTraits<datatype>::cpp_type cpp_type;

  static const DataType type = datatype;
  static const size_t size = sizeof(cpp_type);
  static const bool fixed_length = !std::is_same<cpp_type, Slice>::value;
};

class Variant {
 public:
  Variant(DataType type, const void *value) {
    Reset(type, value);
  }

  ~Variant() {
    Clear();
  }

  template<DataType Type>
  void Reset(const typename DataTypeTraits<Type>::cpp_type& value) {
    Reset(Type, &value);
  }

  // Set the variant to the specified type/value.
  // The value must be of the relative type.
  // In case of strings, the value must be a pointer to a Slice, and the data block
  // will be copied, and released by the variant on the next set/clear call.
  //
  //  Examples:
  //      uint16_t u16 = 512;
  //      Slice slice("Hello World");
  //      variant.set(UINT16, &u16);
  //      variant.set(STRING, &slice);
  void Reset(DataType type, const void *value) {
    CHECK(value != NULL) << "Variant value must be not NULL";
    Clear();
    type_ = type;
    switch (type_) {
      case DataType::UNKNOWN_DATA:
        LOG(FATAL) << "Unreachable";
      case DataType::BOOL:
        numeric_.b1 = *static_cast<const bool *>(value);
        break;
      case DataType::INT8:
        numeric_.i8 = *static_cast<const int8_t *>(value);
        break;
      case DataType::UINT8:
        numeric_.u8 = *static_cast<const uint8_t *>(value);
        break;
      case DataType::INT16:
        numeric_.i16 = *static_cast<const int16_t *>(value);
        break;
      case DataType::UINT16:
        numeric_.u16 = *static_cast<const uint16_t *>(value);
        break;
      case DataType::INT32:
        numeric_.i32 = *static_cast<const int32_t *>(value);
        break;
      case DataType::UINT32:
      case DataType::DATE:
        numeric_.u32 = *static_cast<const uint32_t *>(value);
        break;
      case DataType::TIMESTAMP:
      case DataType::TIME:
      case DataType::INT64:
        numeric_.i64 = *static_cast<const int64_t *>(value);
        break;
      case DataType::UINT64:
        numeric_.u64 = *static_cast<const uint64_t *>(value);
        break;
      case DataType::FLOAT:
        numeric_.float_val = *static_cast<const float *>(value);
        break;
      case DataType::DOUBLE:
        numeric_.double_val = *static_cast<const double *>(value);
        break;
      case DataType::STRING: FALLTHROUGH_INTENDED;
      case DataType::INET: FALLTHROUGH_INTENDED;
      case DataType::UUID: FALLTHROUGH_INTENDED;
      case DataType::TIMEUUID: FALLTHROUGH_INTENDED;
      case DataType::FROZEN: FALLTHROUGH_INTENDED;
      case DataType::JSONB: FALLTHROUGH_INTENDED;
      case DataType::BINARY:
        {
          const Slice *str = static_cast<const Slice *>(value);
          // In the case that str->size() == 0, then the 'Clear()' above has already
          // set vstr_ to Slice(""). Otherwise, we need to allocate and copy the
          // user's data.
          if (str->size() > 0) {
            auto blob = new uint8_t[str->size()];
            memcpy(blob, str->data(), str->size());
            vstr_ = Slice(blob, str->size());
          }
        }
        break;
      case DataType::MAP: FALLTHROUGH_INTENDED;
      case DataType::SET: FALLTHROUGH_INTENDED;
      case DataType::LIST:
        LOG(FATAL) << "Default values for collection types not supported, found: "
                   << type_;
      case DataType::DECIMAL: FALLTHROUGH_INTENDED;
      case DataType::USER_DEFINED_TYPE:
        LOG(FATAL) << "Unsupported data type: " << type_;

      default: LOG(FATAL) << "Unknown data type: " << type_;
    }
  }

  // Set the variant to a STRING type.
  // The specified data block will be copied, and released by the variant
  // on the next set/clear call.
  void Reset(const std::string& data) {
    Slice slice(data);
    Reset(DataType::STRING, &slice);
  }

  // Set the variant to a STRING type.
  // The specified data block will be copied, and released by the variant
  // on the next set/clear call.
  void Reset(const char *data, size_t size) {
    Slice slice(data, size);
    Reset(DataType::STRING, &slice);
  }

  // Returns the type of the Variant
  DataType type() const {
    return type_;
  }

  // Returns a pointer to the internal variant value
  // The return value can be casted to the relative type()
  // The return value will be valid until the next set() is called.
  //
  //  Examples:
  //    static_cast<const int32_t *>(variant.value())
  //    static_cast<const Slice *>(variant.value())
  const void *value() const {
    switch (type_) {
      case DataType::UNKNOWN_DATA: LOG(FATAL) << "Attempted to access value of unknown data type";
      case DataType::BOOL:         return &(numeric_.b1);
      case DataType::INT8:         return &(numeric_.i8);
      case DataType::UINT8:        return &(numeric_.u8);
      case DataType::INT16:        return &(numeric_.i16);
      case DataType::UINT16:       return &(numeric_.u16);
      case DataType::INT32:        return &(numeric_.i32);
      case DataType::UINT32:       return &(numeric_.u32);
      case DataType::INT64:        return &(numeric_.i64);
      case DataType::UINT64:       return &(numeric_.u64);
      case DataType::FLOAT:        return (&numeric_.float_val);
      case DataType::DOUBLE:       return (&numeric_.double_val);
      case DataType::STRING:       FALLTHROUGH_INTENDED;
      case DataType::INET:         FALLTHROUGH_INTENDED;
      case DataType::UUID:         FALLTHROUGH_INTENDED;
      case DataType::TIMEUUID:     FALLTHROUGH_INTENDED;
      case DataType::FROZEN:       FALLTHROUGH_INTENDED;
      case DataType::BINARY:       return &vstr_;
      case DataType::MAP: FALLTHROUGH_INTENDED;
      case DataType::SET: FALLTHROUGH_INTENDED;
      case DataType::LIST:
        LOG(FATAL) << "Default values for collection types not supported, found: "
                   << type_;

      case DataType::DECIMAL: FALLTHROUGH_INTENDED;
      case DataType::USER_DEFINED_TYPE:
        LOG(FATAL) << "Unsupported data type: " << type_;

      default: LOG(FATAL) << "Unknown data type: " << type_;
    }
    CHECK(false) << "not reached!";
    return NULL;
  }

  bool Equals(const Variant *other) const {
    if (other == NULL || type_ != other->type_)
      return false;
    return GetTypeInfo(type_)->Compare(value(), other->value()) == 0;
  }

 private:
  void Clear() {
    // No need to delete[] zero-length vstr_, because we always ensure that
    // such a string would point to a constant "" rather than an allocated piece
    // of memory.
    if (vstr_.size() > 0) {
      delete[] vstr_.mutable_data();
      vstr_.clear();
    }
  }

  union NumericValue {
    bool     b1;
    int8_t   i8;
    uint8_t  u8;
    int16_t  i16;
    uint16_t u16;
    int32_t  i32;
    uint32_t u32;
    int64_t  i64;
    uint64_t u64;
    float    float_val;
    double   double_val;
  };

  DataType type_;
  NumericValue numeric_;
  Slice vstr_;

  DISALLOW_COPY_AND_ASSIGN(Variant);
};

}  // namespace yb
