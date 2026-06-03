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
#include <future>
#include <memory>
#include <unordered_set>

#include "yb/client/client_fwd.h"

#include "yb/util/result.h"

namespace yb::tserver {

// Per-tserver cache of immutable per-db attributes. Today it stores
// only colocation flags.
//
// Lifetime invariants this cache relies on:
//   * Database OIDs are never reused (each DB owns a persisted, monotonically
//     increasing next_normal_pg_oid in master sys.catalog).
//   * Colocation status cannot change once a database has been created.
// Together these mean the only invalidation event we care about is DROP
// DATABASE, which we receive via heartbeats similar to catalog version map
// updates.
class PgDbCache {
 public:
  struct ColocationInfo {
    bool colocated = false;
    bool legacy_colocated_database = true;
  };

  explicit PgDbCache(std::shared_future<client::YBClient*> client_future);
  ~PgDbCache();

  // Returns info for db oid from cache. If no cache entry is present, it is
  // fetched from master and cached before returning.
  // Concurrent uncached requests for same db are coalesced into a single fetch.
  Result<ColocationInfo> GetColocationInfo(uint32_t db_oid);

  // Drops cached entries for the given db_oids
  void Invalidate(const std::unordered_set<uint32_t>& db_oids_deleted);

  // Drops all cached entries.
  void Clear();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
