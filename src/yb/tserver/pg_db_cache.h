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
#include <functional>
#include <future>
#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/tserver/pg_cache_util.h"

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

  using Callback = std::function<void(Result<ColocationInfo>)>;

  // Looks up info for db_oid and passes it to callback. On a cache hit the callback runs before
  // this returns; otherwise it runs on an RPC callback thread once the master has answered.
  // Concurrent uncached requests for the same db are coalesced into a single fetch.
  //
  // This never waits for the master's reply. Callers run on RPC service threads, and that reply
  // is delivered by the same thread pool, so a service thread that waited here could hold up the
  // very reply it is waiting for. See issue #36104. A miss does still wait on the YBClient
  // future, which is unresolved only until the tserver has finished starting up.
  void GetColocationInfo(uint32_t db_oid, Callback callback);

  // Drops cached entries for the given db_oids
  void Invalidate(const PgTableCacheInvalidationFilter::DroppedDbs& dropped_dbs);

  // Drops all cached entries.
  void Clear();

 private:
  class Impl;
  // Shared rather than unique because an in-flight master lookup holds a reference until its
  // callback has run.
  std::shared_ptr<Impl> impl_;
};

}  // namespace yb::tserver
