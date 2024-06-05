// Copyright (c) YugabyteDB, Inc.
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

#include "yb/dockv/value_packing_v2.h"

#include "yb/dockv/packed_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_packing.h"

namespace yb::dockv {

namespace {

#define GET_SET_PRIMITIVE(name, type) \
template <class PB> \
auto DoGetPrimitive(const PB& pb, type*) { \
  return pb.BOOST_PP_CAT(name, _value)(); \
} \
template <class PB> \
auto DoSetPrimitive(type value, PB* pb) { \
  return pb->BOOST_PP_CAT(BOOST_PP_CAT(set_, name), _value)(value); \
}

GET_SET_PRIMITIVE(bool, bool);
GET_SET_PRIMITIVE(double, double);
GET_SET_PRIMITIVE(float, float);
GET_SET_PRIMITIVE(int8, int8_t);
GET_SET_PRIMITIVE(int16, int16_t);
GET_SET_PRIMITIVE(int32, int32_t);
GET_SET_PRIMITIVE(int64, int64_t);
GET_SET_PRIMITIVE(uint32, uint32_t);
GET_SET_PRIMITIVE(uint64, uint64_t);

template <class T, class PB>
T GetPrimitive(const PB& pb) {
  return DoGetPrimitive(pb, static_cast<T*>(nullptr));
}

template <class T, class PB>
void SetPrimitive(T t, PB* pb) {
  return DoSetPrimitive(t, pb);
}

template <class Value>
struct PackQLValueVisitor {
  const Value& value;
  ValueBuffer* out;

  void Binary() const {
    out->append(value.binary_value());
  }

  void Decimal() const {
    out->append(std::string_view(value.decimal_value()));
  }

  void String() const {
    out->append(std::string_view(value.string_value()));
  }

  template <class T>
  void Primitive() const {
    auto primitive_value = GetPrimitive<T>(value);
    out->append(pointer_cast<const char*>(&primitive_value), sizeof(T));
  }
};

template <class Value>
struct PackedQLValueSizeVisitor {
  const Value& value;

  ssize_t Binary() const {
    return value.binary_value().size();
  }

  ssize_t Decimal() const {
    return value.decimal_value().size();
  }

  ssize_t String() const {
    return value.string_value().size();
  }

  template <class T>
  ssize_t Primitive() const {
    return sizeof(T);
  }
};

struct PackedAsVarlenVisitor {
  bool Binary() const {
    return true;
  }

  bool Decimal() const {
    return true;
  }

  bool String() const {
    return true;
  }

  template <class T>
  bool Primitive() const {
    return false;
  }
};

struct UnpackQLValueVisitor {
  Slice value;

  Result<QLValuePB> Binary() const {
    QLValuePB result;
    result.set_binary_value(value.cdata(), value.size());
    return result;
  }

  Result<QLValuePB> Decimal() const {
    QLValuePB result;
    result.set_decimal_value(value.cdata(), value.size());
    return result;
  }

  Result<QLValuePB> String() const {
    QLValuePB result;
    result.set_string_value(value.cdata(), value.size());
    return result;
  }

  template <class T>
  Result<QLValuePB> Primitive() const {
    QLValuePB result;
    RSTATUS_DCHECK_EQ(value.size(), sizeof(T), Corruption, "Wrong encoded value size");
    T primitive_value;
#ifdef IS_LITTLE_ENDIAN
    memcpy(&primitive_value, value.data(), sizeof(T));
#else
    #error "Big endian not implemented"
#endif
    SetPrimitive(primitive_value, &result);
    return result;
  }
};

} // namespace

bool PackedAsVarlen(DataType data_type) {
  return data_type != DataType::NULL_VALUE_TYPE &&
         VisitDataType(data_type, PackedAsVarlenVisitor());
}

void PackQLValueV2(const QLValuePB& value, DataType data_type, ValueBuffer* out) {
  VisitDataType(
      data_type, PackQLValueVisitor<QLValuePB> { .value = value, .out = out });
}

void PackQLValueV2(const LWQLValuePB& value, DataType data_type, ValueBuffer* out) {
  VisitDataType(
      data_type, PackQLValueVisitor<LWQLValuePB> { .value = value, .out = out });
}

Result<QLValuePB> UnpackQLValue(PackedValueV2 value, DataType data_type) {
  if (value.IsNull()) {
    return QLValuePB();
  }
  return VisitDataType(data_type, UnpackQLValueVisitor { .value = *value });
}

ssize_t PackedQLValueSizeV2(const QLValuePB& value, DataType data_type) {
  return VisitDataType(data_type, PackedQLValueSizeVisitor<QLValuePB> { .value = value });
}

ssize_t PackedQLValueSizeV2(const LWQLValuePB& value, DataType data_type) {
  return VisitDataType(data_type, PackedQLValueSizeVisitor<LWQLValuePB> { .value = value });
}

}  // namespace yb::dockv
