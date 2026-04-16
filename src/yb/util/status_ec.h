//
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
//

#pragma once

#include <concepts>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "yb/gutil/endian.h"

#include "yb/util/status.h"

namespace yb {

struct CategoryDescriptor {
  uint8_t id;
  std::string_view name;
};

template <class T>
concept CategoryTag = std::same_as<decltype(T::kCategory), const CategoryDescriptor>;

// Base class for all error tags that use integral representation.
// For instance time duration.
template <class Traits>
class IntegralBackedErrorTag {
  using RepresentationType = typename Traits::RepresentationType;
 public:
  using Value = typename Traits::ValueType;

  static Value Decode(const uint8_t* source) {
    return source
        ? Traits::FromRepresentation(Load<RepresentationType, LittleEndian>(source))
        : Value();
  }

  static size_t DecodeSize(const uint8_t* source) {
    return sizeof(RepresentationType);
  }

  static size_t EncodedSize(Value value) {
    return sizeof(RepresentationType);
  }

  static uint8_t* Encode(Value value, uint8_t* out) {
    Store<RepresentationType, LittleEndian>(out, Traits::ToRepresentation(value));
    return out + sizeof(RepresentationType);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return Traits::ToString(Decode(source));
  }
};

class StringBackedErrorTag {
 public:
  using Value = typename std::string;
  using SizeType = uint64_t;

  static Value Decode(const uint8_t* source);

  static size_t DecodeSize(const uint8_t* source) {
    return Load<SizeType, LittleEndian>(source) + sizeof(SizeType);
  }

  static size_t EncodedSize(const Value& value);

  static uint8_t* Encode(const Value& value, uint8_t* out);

  static std::string DecodeToString(const uint8_t* source) {
    return Decode(source);
  }
};

class StringVectorBackedErrorTag {
 public:
  using Value = typename std::vector<std::string>;
  using SizeType = uint64_t;

  static Value Decode(const uint8_t* source);

  static size_t DecodeSize(const uint8_t* source) {
    return Load<SizeType, LittleEndian>(source);
  }

  static size_t EncodedSize(const Value& value);

  static uint8_t* Encode(const Value& value, uint8_t* out);

  static std::string DecodeToString(const uint8_t* source);
};

template <class Value>
std::string IntegralToString(Value value) {
  if constexpr(std::is_enum_v<Value>) {
    return std::to_string(std::to_underlying(value));
  } else {
    return std::to_string(value);
  }
}

// Base class for error tags that have integral value type.
template <class Value>
class PlainIntegralTraits {
 public:
  using ValueType = Value;
  using RepresentationType = ValueType;

  static ValueType FromRepresentation(RepresentationType source) {
    return source;
  }

  static RepresentationType ToRepresentation(ValueType value) {
    return value;
  }

  static std::string ToString(ValueType value) {
    return IntegralToString(value);
  }
};

template <class ValueType>
using IntegralErrorTag = IntegralBackedErrorTag<PlainIntegralTraits<ValueType>>;

// Extra error code assigned to status.
class StatusErrorCode {
 public:
  virtual uint8_t Category() const = 0;
  virtual size_t EncodedSize() const = 0;
  // Serialization should not be changed after error code is released, since it is
  // transferred over the wire.
  virtual uint8_t* Encode(uint8_t* out) const = 0;
  virtual std::string Message() const = 0;

  virtual ~StatusErrorCode() = default;
};

namespace status_ec::internal {

template <CategoryTag Tag>
class StatusErrorCodeImpl : public StatusErrorCode {
 public:
  using Value = typename Tag::Value;
  // Category is a part of the wire protocol.
  // So it should not be changed after first release containing this category.
  // All used categories could be listed with the following command:
  // git grep -h -F "CategoryDescriptor kCategory" | awk -F'[ {,]' '{print $7}' | sort -n
  static constexpr auto kCategory = Tag::kCategory.id;

  explicit StatusErrorCodeImpl(const Value& value) : value_(value) {}

  explicit StatusErrorCodeImpl(Value&& value) : value_(std::move(value)) {}

  explicit StatusErrorCodeImpl(const Status& status) : value_(Tag::Decode(Data(status))) {}

  uint8_t Category() const override { return kCategory; }

  size_t EncodedSize() const override { return Tag::EncodedSize(value_); }

  uint8_t* Encode(uint8_t* out) const override { return Tag::Encode(value_, out); }

  std::string Message() const override { return Tag::ToMessage(value_); }

  const Value& value() const { return value_; }

  static std::optional<StatusErrorCodeImpl> FromStatus(const Status& status) {
    return DecodeOptional<StatusErrorCodeImpl>(status);
  }

  static std::optional<Value> ValueFromStatus(const Status& status) {
    return DecodeOptional(status);
  }

 private:
  static auto* Data(const Status& status) { return status.ErrorData(kCategory); }

  template <class T = Value>
  static std::optional<T> DecodeOptional(const Status& status) {
    const auto* data = status.ErrorData(kCategory);
    return data ? std::optional<T>(Tag::Decode(data)) : std::nullopt;
  }

  Value value_;
};

template <CategoryTag Tag>
auto IsStatusErrorCodeImplHelper(const StatusErrorCodeImpl<Tag>&) { return true; }

template <class T>
concept IsStatusErrorCodeImpl = requires (T t) { IsStatusErrorCodeImplHelper(t); };

template <class T>
const auto& FetchValue(const T& t) {
  if constexpr (IsStatusErrorCodeImpl<T>) {
    return t.value();
  } else {
    return t;
  }
}

template <class T>
using ValueType = decltype(FetchValue(std::declval<T>()));

template <class T1, class T2>
concept IsValidComparisionPair =
    (IsStatusErrorCodeImpl<T1> || IsStatusErrorCodeImpl<T2>) &&
    std::same_as<ValueType<T1>, ValueType<T2>>;

template <class T1, class T2>
requires(IsValidComparisionPair<T1, T2>)
bool operator==(const T1& lhs, const T2& rhs) { return FetchValue(lhs) == FetchValue(rhs); }

template <class T1, class T2>
requires(IsValidComparisionPair<T1, T2>)
bool operator!=(const T1& lhs, const T2& rhs) { return FetchValue(lhs) != FetchValue(rhs); }

} // namespace status_ec::internal

template <CategoryTag Tag>
struct StatusCategoryRegisterer {
  using Type = status_ec::internal::StatusErrorCodeImpl<Tag>;

 private:
  inline static const auto kRegistration = [] {
    Status::RegisterCategory(
        {Tag::kCategory.id, Tag::kCategory.name, &Tag::DecodeSize, &Tag::DecodeToString });
    return true;
  }();

  static_assert(!std::same_as<decltype([] { return kRegistration; }()), void>);
};

template <CategoryTag Tag>
using StatusErrorCodeImpl = StatusCategoryRegisterer<Tag>::Type;

}  // namespace yb
