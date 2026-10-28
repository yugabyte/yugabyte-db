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

#include <optional>

#include "yb/common/hybrid_time.h"
// For the complete PgCatalogVersion, which DbOidToCatalogVersionMap's member declaration below
// needs; master_fwd.h only forward-declares it.
#include "yb/common/pg_catversions.h"

#include "yb/gutil/macros.h"

#include "yb/master/master_fwd.h"

#include "yb/util/locks.h"

namespace yb::master {

// Master-side cache of pg_yb_catalog_version, plus the pg_yb_invalidation_messages rows that go
// with it. The master leader serves both to tservers in heartbeat responses.
//
// Two paths install snapshots here, and both read a *full* snapshot of the table: the periodic
// background refresh, and the DDL-commit object lock release path, which reads the table directly
// because it also broadcasts the new versions straight to every tserver. This class exists to hold
// the invariants that tie those two writers together, which previously lived as comments spread
// across CatalogManager and which have twice been violated:
//
//   * All cached state is reset together (Reset()). Clearing a subset has produced real bugs:
//     #33596 left the fingerprint behind, which suppressed the next invalidation-messages re-read.
//
//   * A snapshot is installed only if it is newer than the cached one, ordered by the sys-catalog
//     hybrid time the snapshot was read at -- not by the order in which installers happen to
//     arrive. Ordering by arrival would let the heartbeat report a version below one already
//     broadcast to tservers, which the tserver's staleness check treats as divergence between
//     master and tserver and crashes on (#33572).
//
//   * A snapshot read before a Reset() must never be installed after it. A leader stepdown resets
//     the cache; installing a pre-stepdown snapshot afterwards would resurrect it, and if this
//     master then regains leadership it would serve that stale snapshot to tservers another leader
//     has already moved forward. Callers capture Generation() *before* reading and pass it to
//     Install().
//
//   * A caller that advances the versions must install messages read alongside them, or mark the
//     messages unavailable with nullopt. Leaving an older non-null map in place would leave
//     NeedsMessagesRefresh() looking at a matching fingerprint and a present map, so the refresh
//     would stop re-reading pg_yb_invalidation_messages until the next version bump. This is why
//     both callers pass nullopt when their messages read fails, rather than leaving the messages
//     alone.
//
// Thread-safe. Performs no I/O: callers read the catalog tables and hand the result here, which is
// what makes the ordering and reset rules above testable without a cluster.
class PgCatalogVersionsCache {
 public:
  PgCatalogVersionsCache() = default;

  // Generation of the currently cached state, bumped by every Reset(). A caller that reads a
  // snapshot captures this first and hands it back to Install(), which declines an install whose
  // snapshot predates a reset.
  uint64_t Generation() const EXCLUDES(mutex_);

  // Clears the cached state and bumps the generation. Called when this master stops maintaining
  // the cache, i.e. on leader stepdown.
  //
  // Note it empties the versions map rather than dropping it, so GetAll() keeps reporting a hit
  // afterwards -- an empty one.
  void Reset() EXCLUDES(mutex_);

  // Installs a full snapshot of pg_yb_catalog_version read at 'read_ht'. Declines, returning false,
  // if the cache generation has moved on from 'generation' (a reset happened since the read), or if
  // it already holds a snapshot taken at or after 'read_ht'.
  //
  // 'fingerprint' must be the fingerprint of 'versions': it is stored and returned as given, never
  // recomputed. master_heartbeat_service sends no catalog versions at all when it equals the
  // fingerprint a tserver reports, so pairing it with different versions makes tservers either miss
  // an update or receive the full map on every heartbeat.
  //
  // 'update_messages' selects what happens to the invalidation messages: false leaves them
  // untouched, true replaces them with 'messages', where std::nullopt marks them unavailable so
  // that the next refresh re-reads them.
  //
  // 'read_ht' must be valid; an invalid one is refused, since HybridTime::kInvalid is kMax - 1 and
  // would both install out of order and disarm the ordering check for the next install.
  bool Install(
      uint64_t generation, HybridTime read_ht, DbOidToCatalogVersionMap versions,
      uint64_t fingerprint, bool update_messages,
      std::optional<DbOidVersionToMessageListMap> messages) EXCLUDES(mutex_);

  // Copies out the cached versions, and the fingerprint and the snapshot's read time when those are
  // set. Returns false only before the first Install(), in which case the outputs are untouched and
  // the caller falls back to reading the table. After a Reset() that follows an Install() it
  // returns true with an empty map and an invalid read time; see Reset().
  //
  // Reporting the snapshot's read time, not the current time, is what lets the heartbeat tell a
  // tserver that these versions are old. Without it a frozen cache is indistinguishable from a
  // fresh read that genuinely disagrees with the tserver.
  bool GetAll(
      DbOidToCatalogVersionMap* versions, uint64_t* fingerprint,
      HybridTime* read_ht = nullptr) const EXCLUDES(mutex_);

  // Copies out one database's cached versions. Returns false before the first Install(), after a
  // Reset(), or when the database is absent; the outputs are untouched in all three cases.
  bool GetForDb(uint32_t db_oid, uint64_t* catalog_version, uint64_t* last_breaking_version) const
      EXCLUDES(mutex_);

  // Cached invalidation messages, or an empty map if they are unavailable -- either because
  // nothing has been installed yet, or because the last read of pg_yb_invalidation_messages
  // failed. An empty map is safe: messages are only an optimization, and without them a PG backend
  // falls back to a full catalog cache refresh.
  DbOidVersionToMessageListMap GetInvalMessages() const EXCLUDES(mutex_);

  // Whether a refresh that just read 'fingerprint' needs to re-read pg_yb_invalidation_messages:
  // true if the versions changed, or if the messages are marked unavailable. Both conditions are
  // evaluated under one lock so they cannot disagree with each other. It does not consider
  // --ysql_yb_enable_invalidation_messages, so the caller must still gate on that flag.
  bool NeedsMessagesRefresh(uint64_t fingerprint) const EXCLUDES(mutex_);

 private:
  mutable rw_spinlock mutex_;

  // Nullopt until the first Install(), and an empty map after a Reset() -- engaged either way once
  // anything has been installed, which is what makes GetAll() report a hit post-reset.
  std::optional<DbOidToCatalogVersionMap> versions_ GUARDED_BY(mutex_);
  // Only meaningful while versions_ holds a value.
  uint64_t fingerprint_ GUARDED_BY(mutex_) = 0;
  // Nullopt means the last read of pg_yb_invalidation_messages failed, so the next refresh must
  // retry it. Distinct from an empty map, which means the table genuinely has no messages.
  std::optional<DbOidVersionToMessageListMap> inval_messages_ GUARDED_BY(mutex_);
  // Hybrid time of the read that produced versions_. Invalid means nothing is installed.
  HybridTime read_ht_ GUARDED_BY(mutex_);
  uint64_t generation_ GUARDED_BY(mutex_) = 0;

  DISALLOW_COPY_AND_ASSIGN(PgCatalogVersionsCache);
};

}  // namespace yb::master
