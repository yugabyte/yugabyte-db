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

#include <boost/functional/hash.hpp>

#include "yb/dockv/dockv_fwd.h"
#include "yb/util/compare_util.h"

namespace yb::docdb {

// ObjectLockPrefix is the entity for which the ts_local_lock_manager acquires locks. In context of
// object/table locks, when a session requests lock(s) on an object oid corresponding to a database
// oid, an 'ObjectLockPrefix' in formed which is then passed to the ObjectLockManager.
struct ObjectLockPrefix {
  ObjectLockPrefix(
      uint64_t database_oid_, uint64_t object_oid_, dockv::KeyEntryType lock_type_)
      : database_oid(database_oid_), object_oid(object_oid_), lock_type(lock_type_) {}

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(database_oid, object_oid, lock_type);
  }

  uint64_t database_oid;
  uint64_t object_oid;
  dockv::KeyEntryType lock_type;
};

inline bool operator==(const ObjectLockPrefix& lhs, const ObjectLockPrefix& rhs) {
  return YB_STRUCT_EQUALS(database_oid, object_oid, lock_type);
}

inline bool operator<(const ObjectLockPrefix& lhs, const ObjectLockPrefix& rhs) {
  if (lhs.database_oid != rhs.database_oid) {
    return lhs.database_oid < rhs.database_oid;
  }
  if (lhs.object_oid != rhs.object_oid) {
    return lhs.object_oid < rhs.object_oid;
  }
  return lhs.lock_type < rhs.lock_type;
}

inline size_t hash_value(const ObjectLockPrefix object) noexcept {
  size_t seed = 0;
  boost::hash_combine(seed, object.database_oid);
  boost::hash_combine(seed, object.object_oid);
  boost::hash_combine(seed, object.lock_type);
  return seed;
}

} // namespace yb::docdb
