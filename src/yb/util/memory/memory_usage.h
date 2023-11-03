//
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
//

#pragma once

#include <string>
#include <type_traits>

#include <boost/container/small_vector.hpp>
#include <boost/core/enable_if.hpp>
#include <boost/type_traits.hpp>

#include <google/protobuf/message.h>

#include "yb/util/type_traits.h"

namespace yb {

// If there is DynamicMemoryUsage member or DynamicMemoryUsageOf free function for the
// class - use it.
HAS_MEMBER_FUNCTION(DynamicMemoryUsage);
HAS_FREE_FUNCTION(DynamicMemoryUsageOf);

template <class T> requires (HasMemberFunction_DynamicMemoryUsage<T>::value)
std::size_t DynamicMemoryUsageOf(const T& value) {
  return value.DynamicMemoryUsage();
}

template <class Collection>
std::size_t DynamicMemoryUsageOfCollection(const Collection& collection);

template <class Int>
typename std::enable_if<std::is_integral<typename std::remove_reference<Int>::type>::value,
                        std::size_t>::type DynamicMemoryUsageOf(const Int& value) {
  return 0;
}

template <class Int>
typename std::enable_if<std::is_integral<typename std::remove_reference<Int>::type>::value,
                        std::size_t>::type DynamicMemoryUsageOf(const std::atomic<Int>& value) {
  return 0;
}

// std::string uses internal capacity for up to kStdStringInternalSize bytes and only allocates
// memory from heap for bigger strings. Exact memory allocation behaviour depends on C++ std
// library implementation.

#if defined(__clang__)

constexpr const auto kStdStringInternalCapacity = 22;

inline std::size_t DynamicMemoryUsageOf(const std::string& value) {
  const auto capacity = value.capacity();
  if (capacity <= kStdStringInternalCapacity) {
    return 0;
  } else {
    // std::string allocates 16*n bytes for capacity from [16*(n - 1); 16*n - 1].
    // 48 bytes for capacity in [32; 47], 64 bytes for capacity in [48; 63] and so on...
    return (capacity + 16) & ~(size_t(0xf));
  }
}

#elif (__GNUC__ >= 9 && __GNUC__ < 11)

inline std::size_t DynamicMemoryUsageOf(const std::string& value) {
  const auto capacity = value.capacity();
  if (capacity == 0) {
    return 0;
  } else {
    return capacity + 25;
  }
}

#else

constexpr const auto kStdStringInternalCapacity = 15;

inline std::size_t DynamicMemoryUsageOf(const std::string& value) {
  const auto capacity = value.capacity();
  if (capacity <= kStdStringInternalCapacity) {
    return 0;
  } else {
    return capacity + 1;
  }
}

#endif // defined(__clang__)

template <class T>
typename boost::enable_if<boost::is_base_of<google::protobuf::Message, T>, std::size_t>::type
DynamicMemoryUsageOf(const T& value) {
  // SpaceUsedLong() includes both dynamic memory and sizeof(Type), so we need to subtract.
  return value.SpaceUsedLong() - sizeof(T);
}

template <class Pointer>
typename std::enable_if<IsPointerLike<Pointer>::value, std::size_t>::type
DynamicMemoryUsageOf(const Pointer& value) {
  return value ? (value->ObjectSize() + DynamicMemoryUsageOf(*value)) : 0;
}

// Could be used instead of DynamicMemoryUsage on pointer-like objects when there is no
// ObjectSize() implementation in inner objects, but we already know that inner objects are of
// type which is used in pointer-like object definition, but not some subclasses. So we can
// compute size using sizeof().
template <class Pointer>
typename std::enable_if<IsPointerLike<Pointer>::value, std::size_t>::type
DynamicMemoryUsageAllowSizeOf(const Pointer& value) {
  return value ? (sizeof(value) + DynamicMemoryUsageOf(*value)) : 0;
}

// Get dynamic memory usage by entries of small_vector, but don't take into account entries inner
// dynamic memory usage.
// This is used to calculate memory usage by small_vector of pointers pointing to data which we
// don't own, so this data memory usage should be tracked by owner.
template <class T, size_t InternalCapacity>
size_t GetFlatDynamicMemoryUsageOf(
    const boost::container::small_vector<T, InternalCapacity>& value) {
  return value.capacity() > value.internal_capacity() ? value.capacity() * sizeof(T) : 0;
}

template <class T, size_t InternalCapacity>
size_t DynamicMemoryUsageOf(const boost::container::small_vector<T, InternalCapacity>& value) {
  size_t result = GetFlatDynamicMemoryUsageOf(value);
  for (const auto& entry : value) {
    result += DynamicMemoryUsageOf(entry);
  }
  return result;
}

template <class T>
    requires (IsCollection<T>::value && !HasMemberFunction_DynamicMemoryUsage<T>::value)
std::size_t DynamicMemoryUsageOf(const T& value) {
  return DynamicMemoryUsageOfCollection(value);
}

template <class Collection>
std::size_t DynamicMemoryUsageOfCollection(const Collection& collection) {
  std::size_t result = collection.capacity() * sizeof(typename Collection::value_type);
  for (const auto& item : collection) {
    result += DynamicMemoryUsageOf(item);
  }
  return result;
}

template <typename T, typename... Types> requires (sizeof...(Types) > 0)
std::size_t DynamicMemoryUsageOf(const T& entity, const Types&... rest_entities) {
  return DynamicMemoryUsageOf(entity) + DynamicMemoryUsageOf(rest_entities...);
}

template <typename T>
std::size_t DynamicMemoryUsageOrSizeOf(const T& value) {
  if constexpr (HasFreeFunction_DynamicMemoryUsageOf<T>::value) {
    return DynamicMemoryUsageOf(value);
  } else {
    return sizeof(value);
  }
}

}  // namespace yb
