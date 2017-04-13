//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_TOSTRING_H
#define YB_UTIL_TOSTRING_H

#include <string>
#include <type_traits>

#include <boost/tti/has_type.hpp>

// This utility actively use SFINAE (http://en.cppreference.com/w/cpp/language/sfinae)
// technique to route ToString to correct implementation.
namespace yb {
namespace util {

#define HAS_MEMBER_FUNCTION(function) \
    template<class T> \
    struct BOOST_PP_CAT(HasMemberFunction, function) { \
      typedef int Yes; \
      typedef struct { Yes array[2]; } No; \
      typedef typename std::remove_reference<T>::type StrippedT; \
      template<class U> static Yes Test(decltype(static_cast<U*>(nullptr)->function())*); \
      template<class U> static No Test(...); \
      static const bool value = sizeof(Yes) == sizeof(Test<StrippedT>(nullptr)); \
    };

// If class has ToString member function - use it.
HAS_MEMBER_FUNCTION(ToString);

template <class T>
typename std::enable_if<HasMemberFunctionToString<T>::value, std::string>::type
ToString(const T& value) {
  return value.ToString();
}

// If class has ShortDebugString member function - use it. For protobuf classes mostly.
HAS_MEMBER_FUNCTION(ShortDebugString);

template <class T>
typename std::enable_if<HasMemberFunctionShortDebugString<T>::value, std::string>::type
ToString(const T& value) {
  return value.ShortDebugString();
}

// Various variants of integer types.
template <class Int>
typename std::enable_if<(sizeof(Int) > 4) && std::is_signed<Int>::value, char*>::type
IntToBuffer(Int value, char* buffer) {
  return FastInt64ToBufferLeft(value, buffer);
}

template <class Int>
typename std::enable_if<(sizeof(Int) > 4) && !std::is_signed<Int>::value, char*>::type
IntToBuffer(Int value, char* buffer) {
  return FastUInt64ToBufferLeft(value, buffer);
}

template <class Int>
typename std::enable_if<(sizeof(Int) <= 4) && std::is_signed<Int>::value, char*>::type
IntToBuffer(Int value, char* buffer) {
  return FastInt32ToBufferLeft(value, buffer);
}

template <class Int>
typename std::enable_if<(sizeof(Int) <= 4) && !std::is_signed<Int>::value, char*>::type
IntToBuffer(Int value, char* buffer) {
  return FastUInt32ToBufferLeft(value, buffer);
}

template <class Int>
typename std::enable_if<std::is_integral<typename std::remove_reference<Int>::type>::value,
                        std::string>::type ToString(Int&& value) {
  char buffer[kFastToBufferSize];
  auto end = IntToBuffer(value, buffer);
  return std::string(buffer, end);
}

template <class Pointer>
std::string PointerToString(Pointer&& ptr) {
  if (ptr) {
    char buffer[kFastToBufferSize]; // kFastToBufferSize has enough extra capacity for 0x and ->
    buffer[0] = '0';
    buffer[1] = 'x';
    FastHex64ToBuffer(reinterpret_cast<size_t>(&*ptr), buffer + 2);
    char * end = buffer + strlen(buffer);
    memcpy(end, " -> ", 4);
    return buffer + ToString(*ptr);
  } else {
    return "<NULL>";
  }
}

// This class is used to determine whether T is similar to pointer.
// We suppose that if class provides * and -> operators so it is pointer.
template<class T>
class IsPointerLikeHelper {
 private:
  typedef int Yes;
  typedef struct { Yes array[2]; } No;

  template <typename C> static Yes HasDeref(decltype(&C::operator*));
  template <typename C> static No HasDeref(...);

  template <typename C> static Yes HasArrow(decltype(&C::operator->));
  template <typename C> static No HasArrow(...);
 public:
  typedef boost::mpl::bool_<sizeof(HasDeref<T>(nullptr)) == sizeof(Yes) &&
                            sizeof(HasArrow<T>(nullptr)) == sizeof(Yes)> type;
};

template<class T>
class IsPointerLikeImpl : public IsPointerLikeHelper<T>::type {};

template<class T>
class IsPointerLikeImpl<T*> : public boost::mpl::true_ {};

// For correct routing we should strip reference and const, volatile specifiers.
template<class T>
class IsPointerLike : public IsPointerLikeImpl<
    typename std::remove_cv<typename std::remove_reference<T>::type>::type> {
};

template <class Pointer>
typename std::enable_if<IsPointerLike<Pointer>::value,
                        std::string>::type ToString(Pointer&& value) {
  return PointerToString(value);
}

inline const std::string& ToString(const std::string& str) { return str; }
inline std::string ToString(const char* str) { return str; }

template <class First, class Second>
std::string ToString(const std::pair<First, Second>& pair) {
  return "{" + ToString(pair.first) + ", " + ToString(pair.second) + "}";
}

template <class Collection>
std::string CollectionToString(const Collection& collection) {
  std::string result = "[";
  auto first = true;
  for (const auto& item : collection) {
    if (first) {
      first = false;
    } else {
      result += ", ";
    }
    result += ToString(item);
  }
  result += "]";
  return result;
}

// We suppose that if class has nested const_iterator then it is collection.
BOOST_TTI_HAS_TYPE(const_iterator);

template <class T>
typename std::enable_if<has_type_const_iterator<T>::value,
                        std::string>::type ToString(const T& value) {
  return CollectionToString(value);
}

} // namespace util
} // namespace yb

#endif // YB_UTIL_TOSTRING_H
