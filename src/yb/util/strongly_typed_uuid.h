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

#pragma once

#include <random>

#include <boost/preprocessor/cat.hpp>

#include "yb/gutil/endian.h"

#include "yb/util/status_fwd.h"
#include "yb/util/slice.h"
#include "yb/util/uuid.h"

// A "strongly-typed UUID" tool. This is needed to prevent passing the wrong UUID as a
// function parameter, and to make callsites more readable by enforcing that MyUuidType is
// specified instead of just UUID. Conversion from strongly-typed UUIDs
// to regular UUIDs is automatic, but the reverse conversion is always explicit.
#define YB_STRONGLY_TYPED_UUID_DECL(TypeName) \
  struct BOOST_PP_CAT(TypeName, _Tag); \
  typedef ::yb::StronglyTypedUuid<BOOST_PP_CAT(TypeName, _Tag)> TypeName; \
  typedef boost::hash<TypeName> BOOST_PP_CAT(TypeName, Hash); \
  Result<TypeName> BOOST_PP_CAT(FullyDecode, TypeName)(const Slice& slice); \
  TypeName BOOST_PP_CAT(TryFullyDecode, TypeName)(const Slice& slice); \
  Result<TypeName> BOOST_PP_CAT(Decode, TypeName)(Slice * slice); \
  Result<TypeName> BOOST_PP_CAT(TypeName, FromString)(const std::string& strval); \
  std::string ToStringHelper(const TypeName& uuid); \
  Result<TypeName> FromStringHelper(TypeName*, const std::string& strval); \
  size_t GetStaticStringSizeHelper(TypeName*);

#define __YB_STRONGLY_TYPED_UUID_IMPL_BASE(TypeName) \
  [[maybe_unused]] Result<TypeName> BOOST_PP_CAT(FullyDecode, TypeName)(const Slice& slice) { \
    return TypeName(VERIFY_RESULT(yb::Uuid::FullyDecode(slice, BOOST_PP_STRINGIZE(TypeName)))); \
  } \
  [[maybe_unused]] TypeName BOOST_PP_CAT(TryFullyDecode, TypeName)(const Slice& slice) { \
    return TypeName(yb::Uuid::TryFullyDecode(slice)); \
  } \
  [[maybe_unused]] Result<TypeName> BOOST_PP_CAT(Decode, TypeName)(Slice * slice) { \
    return TypeName(VERIFY_RESULT(yb::Uuid::Decode(slice))); \
  } \
  [[maybe_unused]] Result<TypeName> FromStringHelper(TypeName*, const std::string& strval) { \
    return BOOST_PP_CAT(TypeName, FromString)(strval); \
  }

#define YB_STRONGLY_TYPED_UUID_IMPL(TypeName) \
  [[maybe_unused]] std::string ToStringHelper(const TypeName& uuid) { \
    return uuid.GetUuid().ToString(); \
  } \
  [[maybe_unused]] Result<TypeName> BOOST_PP_CAT( \
      TypeName, FromString)(const std::string& strval) { \
    return TypeName(VERIFY_RESULT(::yb::Uuid::FromString(strval))); \
  } \
  [[maybe_unused]] size_t GetStaticStringSizeHelper(TypeName*) { return 36; } \
  __YB_STRONGLY_TYPED_UUID_IMPL_BASE(TypeName)

// Same as YB_STRONGLY_TYPED_UUID_IMPL, but converts to and from a hex string (no dashes)
// representation.
#define YB_STRONGLY_TYPED_HEX_UUID_IMPL(TypeName) \
  [[maybe_unused]] std::string ToStringHelper(const TypeName& uuid) { \
    return uuid.GetUuid().ToHexString(); \
  } \
  [[maybe_unused]] Result<TypeName> BOOST_PP_CAT( \
      TypeName, FromString)(const std::string& strval) { \
    return TypeName(VERIFY_RESULT(::yb::Uuid::FromHexString(strval))); \
  } \
  [[maybe_unused]] size_t GetStaticStringSizeHelper(TypeName*) { return 32; } \
  __YB_STRONGLY_TYPED_UUID_IMPL_BASE(TypeName)

#define YB_STRONGLY_TYPED_UUID(TypeName) \
  YB_STRONGLY_TYPED_UUID_DECL(TypeName) \
  YB_STRONGLY_TYPED_UUID_IMPL(TypeName)

namespace yb {

template <class Tag>
class StronglyTypedUuid {
 public:
  StronglyTypedUuid() = default;

  // This is public so that we can construct a strongly-typed UUID value out of a regular one.
  // In that case we'll have to spell out the class name, which will enforce readability.
  explicit StronglyTypedUuid(const Uuid& uuid) : uuid_(uuid) {}

  StronglyTypedUuid(uint64_t pb1, uint64_t pb2) {
    pb1 = LittleEndian::FromHost64(pb1);
    pb2 = LittleEndian::FromHost64(pb2);
    memcpy(uuid_.data(), &pb1, sizeof(pb1));
    memcpy(uuid_.data() + sizeof(pb1), &pb2, sizeof(pb2));
  }

  // Gets the underlying UUID, only if not undefined.
  const boost::uuids::uuid& operator *() const {
    return uuid_.impl();
  }

  const Uuid& GetUuid() const { return uuid_; }

  // Returns true iff the UUID is nil.
  bool IsNil() const {
    return uuid_.IsNil();
  }

  explicit operator bool() const {
    return !IsNil();
  }

  bool operator!() const {
    return IsNil();
  }

  // Represent UUID as pair of uint64 for protobuf serialization.
  // This serialization is independent of the byte order on the machine.
  // For instance we could convert UUID to pair of uint64 on little endian machine, transfer them
  // to big endian machine and UUID created from them will be the same.
  std::pair<uint64_t, uint64_t> ToUInt64Pair() const {
    std::pair<uint64_t, uint64_t> result;
    memcpy(&result.first, uuid_.data(), sizeof(result.first));
    memcpy(&result.second, uuid_.data() + sizeof(result.first), sizeof(result.second));
    return std::pair<uint64_t, uint64_t>(
        LittleEndian::ToHost64(result.first), LittleEndian::ToHost64(result.second));
  }

  // Represents an invalid UUID.
  static StronglyTypedUuid<Tag> Nil() {
    return StronglyTypedUuid(Uuid::Nil());
  }

  // Converts a UUID to a string, returns "<Undefined{ClassName}>" if UUID is undefined, where
  // {ClassName} is the name associated with the Tag class.
  std::string ToString() const { return ToStringHelper(*this); }

  // Converts a string to a StronglyTypedUuid, if such a conversion exists.
  // The empty string maps to undefined.
  static Result<StronglyTypedUuid<Tag>> FromString(const std::string& strval) {
    return FromStringHelper(static_cast<StronglyTypedUuid<Tag>*>(nullptr), strval);
  }

  // Generate a random StronglyTypedUuid.
  static StronglyTypedUuid<Tag> GenerateRandom() {
    return StronglyTypedUuid(Uuid::Generate());
  }

  static StronglyTypedUuid<Tag> GenerateRandom(std::mt19937_64* rng) {
    return StronglyTypedUuid(Uuid::Generate(rng));
  }

  uint8_t* data() {
    return uuid_.data();
  }

  const uint8_t* data() const {
    return uuid_.data();
  }

  size_t size() const {
    return uuid_.size();
  }

  Slice AsSlice() const {
    return Slice(uuid_.AsSlice());
  }

  static size_t StaticSize() {
    return boost::uuids::uuid::static_size();
  }

  static size_t StaticStringSize() {
    return GetStaticStringSizeHelper(static_cast<StronglyTypedUuid<Tag>*>(nullptr));
  }

 private:
  // Represented as an optional UUID.
  Uuid uuid_;
};

template <class Tag>
std::ostream& operator << (std::ostream& out, const StronglyTypedUuid<Tag>& uuid) {
  return out << uuid.ToString();
}

template <class Tag>
bool operator== (const StronglyTypedUuid<Tag>& lhs, const StronglyTypedUuid<Tag>& rhs) noexcept {
  return *lhs == *rhs;
}

template <class Tag>
std::strong_ordering operator <=> (
    const StronglyTypedUuid<Tag>& lhs, const StronglyTypedUuid<Tag>& rhs) noexcept {
  return std::compare_strong_order_fallback(*lhs, *rhs);
}

template <class Tag>
std::size_t hash_value(const StronglyTypedUuid<Tag>& u) noexcept {
  return hash_value(*u);
}

}  // namespace yb

namespace std {
template <class Tag>
struct hash<yb::StronglyTypedUuid<Tag>> {
  size_t operator()(const yb::StronglyTypedUuid<Tag>& strong_uuid) const {
    return yb::hash_value(strong_uuid);
  }
};

}  // namespace std
