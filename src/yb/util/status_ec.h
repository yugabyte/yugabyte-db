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

#include <boost/optional.hpp>

#include "yb/gutil/endian.h"

#include "yb/util/status.h"

namespace yb {

// Base class for all error tags that use integral representation.
// For instance time duration.
template <class Traits>
class IntegralBackedErrorTag {
 public:
  typedef typename Traits::ValueType Value;

  static Value Decode(const uint8_t* source) {
    if (!source) {
      return Value();
    }
    return Traits::FromRepresentation(
        Load<typename Traits::RepresentationType, LittleEndian>(source));
  }

  static size_t DecodeSize(const uint8_t* source) {
    return sizeof(typename Traits::RepresentationType);
  }

  static size_t EncodedSize(Value value) {
    return sizeof(typename Traits::RepresentationType);
  }

  static uint8_t* Encode(Value value, uint8_t* out) {
    Store<typename Traits::RepresentationType, LittleEndian>(out, Traits::ToRepresentation(value));
    return out + sizeof(typename Traits::RepresentationType);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return Traits::ToString(Decode(source));
  }
};

class StringBackedErrorTag {
 public:
  typedef typename std::string Value;
  typedef uint64_t SizeType;

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
  typedef typename std::vector<std::string> Value;
  typedef uint64_t SizeType;

  static Value Decode(const uint8_t* source);

  static size_t DecodeSize(const uint8_t* source) {
    return Load<SizeType, LittleEndian>(source);
  }

  static size_t EncodedSize(const Value& value);

  static uint8_t* Encode(const Value& value, uint8_t* out);

  static std::string DecodeToString(const uint8_t* source);
};

template <class Enum>
typename std::enable_if<std::is_enum<Enum>::value, std::string>::type
IntegralToString(Enum e) {
  return std::to_string(static_cast<typename std::underlying_type<Enum>::type>(e));
}

template <class Value>
typename std::enable_if<!std::is_enum<Value>::value, std::string>::type
IntegralToString(Value value) {
  return std::to_string(value);
}

// Base class for error tags that have integral value type.
template <class Value>
class PlainIntegralTraits {
 public:
  typedef Value ValueType;
  typedef ValueType RepresentationType;

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
struct IntegralErrorTag : public IntegralBackedErrorTag<PlainIntegralTraits<ValueType>> {
};

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

template <class Tag>
class StatusErrorCodeImpl : public StatusErrorCode {
 public:
  typedef typename Tag::Value Value;
  // Category is a part of the wire protocol.
  // So it should not be changed after first release containing this category.
  // All used categories could be listed with the following command:
  // git grep -h -F "uint8_t kCategory" | awk '{ print $6 }' | sort -n
  static constexpr uint8_t kCategory = Tag::kCategory;

  explicit StatusErrorCodeImpl(const Value& value) : value_(value) {}
  explicit StatusErrorCodeImpl(Value&& value) : value_(std::move(value)) {}

  explicit StatusErrorCodeImpl(const Status& status);

  static boost::optional<StatusErrorCodeImpl> FromStatus(const Status& status);

  static boost::optional<Value> ValueFromStatus(const Status& status);

  uint8_t Category() const override {
    return kCategory;
  }

  size_t EncodedSize() const override {
    return Tag::EncodedSize(value_);
  }

  uint8_t* Encode(uint8_t* out) const override {
    return Tag::Encode(value_, out);
  }

  const Value& value() const {
    return value_;
  }

  std::string Message() const override {
    return Tag::ToMessage(value_);
  }

 private:
  Value value_;
};

template <class Tag>
bool operator==(const StatusErrorCodeImpl<Tag>& lhs, const StatusErrorCodeImpl<Tag>& rhs) {
  return lhs.value() == rhs.value();
}

template <class Tag>
bool operator==(const StatusErrorCodeImpl<Tag>& lhs, const typename Tag::Value& rhs) {
  return lhs.value() == rhs;
}

template <class Tag>
bool operator==(const typename Tag::Value& lhs, const StatusErrorCodeImpl<Tag>& rhs) {
  return lhs == rhs.value();
}

template <class Tag>
bool operator!=(const StatusErrorCodeImpl<Tag>& lhs, const StatusErrorCodeImpl<Tag>& rhs) {
  return lhs.value() != rhs.value();
}

template <class Tag>
bool operator!=(const StatusErrorCodeImpl<Tag>& lhs, const typename Tag::Value& rhs) {
  return lhs.value() != rhs;
}

template <class Tag>
bool operator!=(const typename Tag::Value& lhs, const StatusErrorCodeImpl<Tag>& rhs) {
  return lhs != rhs.value();
}

struct StatusCategoryDescription {
  uint8_t id = 0;
  const std::string* name = nullptr;
  std::function<size_t(const uint8_t*)> decode_size;
  std::function<std::string(const uint8_t*)> to_string;

  template <class Tag>
  static StatusCategoryDescription Make(const std::string* name_) {
    return StatusCategoryDescription {
      .id = Tag::kCategory,
      .name = name_,
      .decode_size = &Tag::DecodeSize,
      .to_string = &Tag::DecodeToString
    };
  }
};

class StatusCategoryRegisterer {
 public:
  explicit StatusCategoryRegisterer(const StatusCategoryDescription& description);
};

template <class Tag>
StatusErrorCodeImpl<Tag>::StatusErrorCodeImpl(const Status& status)
    : value_(Tag::Decode(status.ErrorData(Tag::kCategory))) {}

template <class Tag>
boost::optional<StatusErrorCodeImpl<Tag>> StatusErrorCodeImpl<Tag>::FromStatus(
    const Status& status) {
  const auto* error_data = status.ErrorData(Tag::kCategory);
  if (!error_data) {
    return boost::none;
  }
  return StatusErrorCodeImpl<Tag>(Tag::Decode(error_data));
}

template <class Tag>
boost::optional<typename StatusErrorCodeImpl<Tag>::Value> StatusErrorCodeImpl<Tag>::ValueFromStatus(
    const Status& status) {
  const auto* error_data = status.ErrorData(Tag::kCategory);
  if (!error_data) {
    return boost::none;
  }
  return Tag::Decode(error_data);
}

} // namespace yb
