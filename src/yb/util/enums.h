// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_ENUMS_H_
#define YB_UTIL_ENUMS_H_

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/facilities/apply.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

namespace yb {
namespace util {

// Convert a strongly typed enum to its underlying type.
// Based on an answer to this StackOverflow question: https://goo.gl/zv2Wg3
template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) {
  return static_cast<typename std::underlying_type<E>::type>(e);
}

#define YB_ENUM_ITEM(s, data, elem) BOOST_PP_CAT(BOOST_PP_APPLY(data), elem),
#define YB_ENUM_CASE_NAME(s, data, elem) \
  case BOOST_PP_TUPLE_ELEM(2, 0, data):: \
      BOOST_PP_CAT(BOOST_PP_APPLY(BOOST_PP_TUPLE_ELEM(2, 1, data)), elem): \
          return BOOST_PP_STRINGIZE(elem);

#define YB_DEFINE_ENUM_IMPL(enum_name, prefix, list) \
  enum class enum_name { \
    BOOST_PP_SEQ_FOR_EACH(YB_ENUM_ITEM, prefix, list) \
  }; \
  \
  inline const char * ToString(enum_name value) { \
    switch(value) { \
    BOOST_PP_SEQ_FOR_EACH(YB_ENUM_CASE_NAME, (enum_name, prefix), list); \
    } \
    return "unknown " BOOST_PP_STRINGIZE(enum_name); \
  } \
  \
  inline std::ostream& operator<<(std::ostream& out, enum_name value) { \
    return out << ToString(value); \
  } \
  \
  constexpr size_t BOOST_PP_CAT(kElementsIn, enum_name) = BOOST_PP_SEQ_SIZE(list); \
  /**/

#define YB_DEFINE_ENUM(enum_name, list) YB_DEFINE_ENUM_IMPL(enum_name, BOOST_PP_NIL, list)
#define YB_DEFINE_ENUM_EX(enum_name, prefix, list) YB_DEFINE_ENUM_IMPL(enum_name, (prefix), list)

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_ENUMS_H_
