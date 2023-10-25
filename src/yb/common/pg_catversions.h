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

#include "yb/util/logging.h"

#include "yb/gutil/macros.h"
#include "yb/util/hash_util.h"

namespace yb::master {

// This records catalog version information for a given database at master side.
struct PgCatalogVersion {
  uint64_t current_version;
  uint64_t last_breaking_version;
};

} // namespace yb::master

namespace yb::tserver {

// This records catalog version information for a given database at tserver side.
struct CatalogVersionInfo {
  // Latest known version from pg_yb_catalog_version for the database.
  uint64_t current_version;
  uint64_t last_breaking_version;

  // Shared memory array db_catalog_versions_ index of the slot allocated for the database.
  int32_t  shm_index;
};

} // namespace yb::tserver

namespace yb {

template<class CatalogVersionsMap>
uint64_t FingerprintCatalogVersions(const CatalogVersionsMap& versions) {
  std::string serialized;
  serialized.reserve(versions.size() * 32);
  for (const auto& it : versions) {
    const uint32_t db_oid = it.first;
    serialized.append(std::to_string(db_oid))
              .append(",")
              .append(std::to_string(it.second.current_version))
              .append(",")
              .append(std::to_string(it.second.last_breaking_version))
              .append(" ");
  }
  VLOG(2) << "serialized: " << serialized.substr(0, 256);
  // For fingerprint, we do not want to weaken the collision resistance if a new hash
  // function is selected to replace MurmurHash2_64.
  return HashUtil::MurmurHash2_64(serialized.data(), serialized.size(), 0 /* seed */);
}

} // namespace yb
