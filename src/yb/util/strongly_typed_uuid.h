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

#ifndef YB_UTIL_STRONGLY_TYPED_UUID_H
#define YB_UTIL_STRONGLY_TYPED_UUID_H

#include <boost/uuid/uuid.hpp>
#include <boost/optional.hpp>
#include <boost/uuid/random_generator.hpp>

#include "yb/util/result.h"

// A "strongly-typed UUID" tool. This is needed to prevent passing the wrong UUID as a
// function parameter, and to make callsites more readable by enforcing that MyUuidType is
// specified instead of just UUID. Conversion from strongly-typed UUIDs
// to regular UUIDs is automatic, but the reverse conversion is always explicit.
#define YB_STRONGLY_TYPED_UUID(TypeName) \
  struct BOOST_PP_CAT(TypeName, _Tag) { static const std::string name; }; \
  const std::string BOOST_PP_CAT(TypeName, _Tag)::name = BOOST_PP_STRINGIZE(TypeName); \
  typedef ::yb::StronglyTypedUuid<BOOST_PP_CAT(TypeName, _Tag)> TypeName;

namespace yb {

template <class Tag>
class StronglyTypedUuid {
 public:
  // Represents an invalid UUID.
  static const StronglyTypedUuid<Tag> kUndefined;

  // This is public so that we can construct a strongly-typed UUID value out of a regular one.
  // In that case we'll have to spell out the class name, which will enforce readability.
  explicit StronglyTypedUuid(boost::optional<boost::uuids::uuid> uuid) : uuid_(uuid) {}

  // Gets the underlying UUID, only if not undefined.
  boost::uuids::uuid operator * () const {
    CHECK(uuid_);
    return uuid_.get();
  };

  // Converts a string to a StronglyTypedUuid, if such a conversion exists.
  // The empty string maps to undefined.
  static Result<StronglyTypedUuid<Tag>> GenerateUuidFromString(const std::string& strval) {
    if (strval == "") {
      return kUndefined;
    }
    try {
      boost::optional<boost::uuids::uuid> uuid = boost::lexical_cast<boost::uuids::uuid>(strval);
      return StronglyTypedUuid(uuid);
    } catch (std::exception& e) {
      return STATUS_SUBSTITUTE(InvalidArgument, "String $0 cannot be converted to a uuid", strval);
    }
  }

  // Generate a random StronglyTypedUuid.
  static StronglyTypedUuid<Tag> GenerateRandomUuid() {
    return StronglyTypedUuid(boost::uuids::random_generator()());
  }

  // Converts a UUID to a string, returns "<Undefined{ClassName}>" if UUID is undefined, where
  // {ClassName} is the name associated with the Tag class.
  std::string ToString() const {
    if (uuid_) {
      return boost::lexical_cast<std::string>(*uuid_);
    } else {
      return "<Undefined" + Tag::name + ">";
    }
  }

  // Returns true iff the UUID is defined.
  bool IsValid() const {
    return static_cast<bool>(uuid_);
  }

 private:
  // Represented as an optional UUID.
  boost::optional<boost::uuids::uuid> uuid_;
};

template <class Tag>
std::ostream& operator << (std::ostream& out, const StronglyTypedUuid<Tag>& uuid) {
  out << uuid.ToString();
  return out;
}

// Returns true iff the StronglyTypedUuids are equal.
template <class Tag>
bool operator == (const StronglyTypedUuid<Tag>& lhs, const StronglyTypedUuid<Tag>& rhs) {
  bool lhs_valid = lhs.IsValid();
  bool rhs_valid = rhs.IsValid();
  if (!lhs_valid && !rhs_valid) {
    return true;
  }
  if (lhs_valid != rhs_valid) {
    return false;
  }
  return *lhs == *rhs;
}

template <class Tag>
bool operator != (const StronglyTypedUuid<Tag>& lhs, const StronglyTypedUuid<Tag>& rhs) {
  return !(lhs == rhs);
}

template <class Tag>
const StronglyTypedUuid<Tag>
StronglyTypedUuid<Tag>::kUndefined(boost::optional<boost::uuids::uuid>{});

} // namespace yb

#endif // YB_UTIL_STRONGLY_TYPED_UUID_H
