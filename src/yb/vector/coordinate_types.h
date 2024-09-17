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

#pragma once

#include <cstdint>

#include <concepts>
#include <type_traits>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/transform.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#include "yb/util/enums.h"

namespace yb::vectorindex {

// The usearch counterpart of this is scalar_kind_t in index_plugins.hpp.
// Columns:
// 1. This goes into enum element naming, e.g. kFloat64 or kFloat32.
// 2. The corresponding standard C/C++ data type or a ..._t type from stdint.h.
// 3. The prefix of what usearch calls this type (e.g. f64_k for the scalar_kind_t enum element and
//    f64_t for the typedef). This is also a convenient short identifer.
#define YB_COORDINATE_TYPE_INFO   \
    /* Floating-point types */    \
    ((Float32, float,    f32))    \
    ((Float64, double,   f64))    \
    /* Signed integer types */    \
    ((Int8,    int8_t,   i8))     \
    ((Int16,   int16_t,  i16))    \
    ((Int32,   int32_t,  i32))    \
    ((Int64,   int64_t,  i64))    \
    /* Unsigned integer types */  \
    ((UInt8,   uint8_t,  u8))     \
    ((UInt16,  uint16_t, u16))    \
    ((UInt32,  uint32_t, u32))    \
    ((UInt64,  uint64_t, u64))

// Convenience macros to extract fields from the tuples above.
#define YB_EXTRACT_COORDINATE_TYPE_NAME_CAPITALIZED(tuple) BOOST_PP_TUPLE_ELEM(3, 0, tuple)
#define YB_EXTRACT_COORDINATE_TYPE(tuple)                  BOOST_PP_TUPLE_ELEM(3, 1, tuple)
#define YB_EXTRACT_COORDINATE_TYPE_SHORT_NAME(tuple)       BOOST_PP_TUPLE_ELEM(3, 2, tuple)

// ------------------------------------------------------------------------------------------------
// CoordinateKind enum
// ------------------------------------------------------------------------------------------------

// Macro to extract the first element of each tuple, and prepend a "k" to it.
#define YB_COORDINATE_ENUM_ELEMENT_NAME(coordinate_info_tuple) \
    BOOST_PP_CAT(k, YB_EXTRACT_COORDINATE_TYPE_NAME_CAPITALIZED(coordinate_info_tuple))

#define YB_COORDINATE_ENUM_ELEMENT_HELPER(s, data, coordinate_info_tuple) \
    YB_COORDINATE_ENUM_ELEMENT_NAME(coordinate_info_tuple)

// Transform the sequence to get a sequence with only the first element and 'k' prepended.
#define YB_COORDINATE_ENUM_ELEMENTS_FOR_DEFINITION \
    BOOST_PP_SEQ_TRANSFORM(YB_COORDINATE_ENUM_ELEMENT_HELPER, _, YB_COORDINATE_TYPE_INFO)

YB_DEFINE_ENUM(CoordinateKind, YB_COORDINATE_ENUM_ELEMENTS_FOR_DEFINITION);

#undef YB_COORDINATE_ENUM_ELEMENT_HELPER
#undef YB_COORDINATE_ENUM_ELEMENTS

// ------------------------------------------------------------------------------------------------
// Concepts
// ------------------------------------------------------------------------------------------------

// Macro to extract the second element of each tuple and wrap it with std::same_as<T, Type>
#define YB_COORDINATE_CONCEPT_ELEMENT(s, type_variable, coordinate_info_tuple) \
    (std::same_as<type_variable, YB_EXTRACT_COORDINATE_TYPE(coordinate_info_tuple)>)

// Transform the sequence to get a sequence of std::same_as<T, Type>
#define YB_COORDINATE_CONCEPT_SEQ \
    BOOST_PP_SEQ_TRANSFORM(YB_COORDINATE_CONCEPT_ELEMENT, T, YB_COORDINATE_TYPE_INFO)

#define YB_JOIN_WITH_OR(s, state, elem) state || elem

template<typename T>
concept CoordinateScalarType =
    BOOST_PP_SEQ_FOLD_LEFT( \
        YB_JOIN_WITH_OR,
        BOOST_PP_SEQ_HEAD(YB_COORDINATE_CONCEPT_SEQ),
        BOOST_PP_SEQ_TAIL(YB_COORDINATE_CONCEPT_SEQ));

template<typename T>
concept IndexableVectorType =
    requires {
        typename T::value_type;  // Ensure T has a value_type
    } && CoordinateScalarType<typename T::value_type> &&
    std::same_as<T, std::vector<typename T::value_type>>;

#undef YB_COORDINATE_CONCEPT_ELEMENT
#undef YB_COORDINATE_CONCEPT_SEQ
#undef YB_JOIN_WITH_OR

// ------------------------------------------------------------------------------------------------
// Coordinate type traits
// ------------------------------------------------------------------------------------------------

template <CoordinateScalarType T>
struct CoordinateTypeTraits {
  using Scalar = T;
  using Vector = std::vector<T>;
};

#define YB_DEFINE_COORDINATE_TYPE_TRAITS( \
        capitalized_name, \
        scalar_type_name, \
        short_type_name) \
    template <> \
    struct CoordinateTypeTraits<scalar_type_name> { \
      static constexpr CoordinateKind kKind = CoordinateKind::BOOST_PP_CAT(k, capitalized_name); \
      static constexpr const char* ShortTypeNameStr() { \
        /* Short type name such as f32, u8, etc. */ \
        return BOOST_PP_STRINGIZE(short_type_name); \
      } \
    };

#define YB_DEFINE_COORDINATE_TYPE_TRAITS_WRAPPER(r, data, coordinate_info_tuple) \
    YB_DEFINE_COORDINATE_TYPE_TRAITS( \
        /* E.g. Float */ YB_EXTRACT_COORDINATE_TYPE_NAME_CAPITALIZED(coordinate_info_tuple), \
        /* E.g. float */ YB_EXTRACT_COORDINATE_TYPE(coordinate_info_tuple), \
        /* E.g. f32 */   YB_EXTRACT_COORDINATE_TYPE_SHORT_NAME(coordinate_info_tuple))

BOOST_PP_SEQ_FOR_EACH(YB_DEFINE_COORDINATE_TYPE_TRAITS_WRAPPER, _, YB_COORDINATE_TYPE_INFO)

#undef YB_DEFINE_COORDINATE_TYPE_TRAITS
#undef YB_DEFINE_COORDINATE_TYPE_TRAITS_WRAPPER

template <IndexableVectorType VectorType>
const char* VectorCoordinateTypeShortStr() {
  return CoordinateTypeTraits<typename VectorType::value_type>::ShortTypeNameStr();
}

// ------------------------------------------------------------------------------------------------
// Mapping CoordinateKind to the actual type
// ------------------------------------------------------------------------------------------------

#define YB_CASE_FOR_COORDINATE_KIND_SWITCH(r, data, coordinate_info_tuple) \
    case CoordinateKind::YB_COORDINATE_ENUM_ELEMENT_NAME(coordinate_info_tuple): \
      /* Default-construct a correctly typed scalar */ \
      return func(YB_EXTRACT_COORDINATE_TYPE(coordinate_info_tuple){});

template <typename ReturnType, typename Functor>
ReturnType HandleCoordinateKindSwitch(CoordinateKind coordinate_kind, Functor&& func) {
  switch (coordinate_kind) {
    BOOST_PP_SEQ_FOR_EACH(YB_CASE_FOR_COORDINATE_KIND_SWITCH, _, YB_COORDINATE_TYPE_INFO)
  }
}

#undef YB_CASE_FOR_COORDINATE_KIND_SWITCH

// ------------------------------------------------------------------------------------------------
// A utility for instantiating templates for all coordinate types
// ------------------------------------------------------------------------------------------------

#define YB_INSTANTIATE_TEMPLATE_FOR_VECTOR_OF(r, template_name, coordinate_type) \
    template class template_name<std::vector<coordinate_type>>;

#define YB_INSTANTIATE_TEMPLATE_FOR_VECTOR_FROM_TUPLE(r, template_name, coordinate_info_tuple) \
    YB_INSTANTIATE_TEMPLATE_FOR_VECTOR_OF( \
        r, template_name, YB_EXTRACT_COORDINATE_TYPE(coordinate_info_tuple))

#define YB_INSTANTIATE_TEMPLATE_FOR_ALL_VECTOR_TYPES(template_name) \
    BOOST_PP_SEQ_FOR_EACH(YB_INSTANTIATE_TEMPLATE_FOR_VECTOR_FROM_TUPLE, \
                          template_name, \
                          YB_COORDINATE_TYPE_INFO)


// ------------------------------------------------------------------------------------------------
// Vector cast
// ------------------------------------------------------------------------------------------------

template<IndexableVectorType ToVector, IndexableVectorType FromVector>
ToVector vector_cast(const FromVector& from_vector) {
  if constexpr (std::is_same_v<FromVector, ToVector>) {
    return from_vector;
  }
  ToVector to_vector;
  to_vector.reserve(from_vector.size());
  for (const auto& from_element : from_vector) {
    to_vector.push_back(static_cast<typename ToVector::value_type>(from_element));
  }
  return to_vector;
}

// Cast for vector of vectors
template<IndexableVectorType ToVector, IndexableVectorType FromVector>
auto vector_cast(const std::vector<FromVector>& v) {
  if constexpr (std::is_same_v<FromVector, ToVector>) {
    return v;
  }
  std::vector<ToVector> result;
  result.reserve(v.size());
  for (const auto& subvector : v) {
    result.push_back(vector_cast<ToVector>(subvector));
  }
  return result;
}


}  // namespace yb::vectorindex
