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

#include "yb/master/pg_catalog_versions_cache.h"

#include <mutex>

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"

DECLARE_bool(TEST_log_catalog_version_cache_events);

namespace yb::master {

uint64_t PgCatalogVersionsCache::Generation() const {
  SharedLock lock(mutex_);
  return generation_;
}

void PgCatalogVersionsCache::Reset() {
  std::lock_guard lock(mutex_);
  // The cached fields are cleared together; clearing only a subset has been a bug (#33596 left the
  // fingerprint behind, which suppressed the next invalidation-messages re-read). Anything added
  // to this class must be cleared here too.
  if (versions_) {
    versions_->clear();
  }
  fingerprint_ = 0;
  // Reset to an empty map rather than nullopt: nullopt means the last read of
  // pg_yb_invalidation_messages failed, which is a different thing from "we are not caching".
  inval_messages_ = DbOidVersionToMessageListMap();
  read_ht_ = HybridTime::kInvalid;
  // Bumped rather than cleared: this is what makes Install() reject a snapshot read before now.
  ++generation_;
  LOG_IF(INFO, PREDICT_FALSE(FLAGS_TEST_log_catalog_version_cache_events))
      << "PgCatalogVersionsCache: reset, generation: " << generation_;
}

bool PgCatalogVersionsCache::Install(
    uint64_t generation, HybridTime read_ht, DbOidToCatalogVersionMap versions,
    uint64_t fingerprint, bool update_messages,
    std::optional<DbOidVersionToMessageListMap> messages) {
  if (!read_ht.is_valid()) {
    // Callers must pass the read time of an actual read. Installing an invalid one would both
    // install out of order -- HybridTime::kInvalid is kMax - 1, so it beats every real snapshot in
    // the comparison below -- and then disarm that comparison for the following install.
    // Note the read time GetAll() reports is the installed snapshot's own, for telling a tserver
    // how old these versions are; only a read of the table supplies one worth installing.
    LOG_WITH_FUNC(DFATAL) << "Refusing to install a catalog versions snapshot with no read time";
    return false;
  }
  std::lock_guard lock(mutex_);
  if (generation != generation_) {
    // The cache was reset after this snapshot was read, i.e. we lost leadership in between.
    // Installing now would undo that reset, and the data is from before another master took over.
    // Logged at INFO, not VLOG: the generation only moves on a leader stepdown, so this fires at
    // most once per in-flight install per stepdown, and it is what explains a cache that stays
    // empty across a failover.
    LOG_WITH_FUNC(INFO) << "Skipping install: cache generation moved from " << generation << " to "
                        << generation_;
    return false;
  }
  // The is_valid() test is required, not defensive: HybridTime::kInvalid is kMax - 1, so an unset
  // read_ht_ compares as nearly maximal rather than as "older than everything". Dropping it would
  // reject every install, leaving the cache permanently empty before the first install and after
  // every Reset(), which are the only two states where read_ht_ is invalid.
  if (read_ht_.is_valid() && read_ht <= read_ht_) {
    // An equal or newer snapshot is already installed. Dropping this one is what keeps the cache
    // monotonic, and it is also correct for the caller that broadcast this snapshot to tservers:
    // what is installed is at least as new, so the cache cannot report below the broadcast.
    VLOG_WITH_FUNC(2) << "Skipping install of snapshot at " << read_ht << ", cache holds "
                      << read_ht_;
    return false;
  }
  versions_ = std::move(versions);
  fingerprint_ = fingerprint;
  read_ht_ = read_ht;
  if (update_messages) {
    // nullopt marks the messages unavailable so the next refresh re-reads them; the versions above
    // are unaffected, since messages are only an optimization on top of them.
    inval_messages_ = std::move(messages);
  }
  LOG_IF(INFO, PREDICT_FALSE(FLAGS_TEST_log_catalog_version_cache_events))
      << "PgCatalogVersionsCache: installed snapshot at " << read_ht
      << ", databases: " << versions_->size();
  return true;
}

bool PgCatalogVersionsCache::GetAll(
    DbOidToCatalogVersionMap* versions, uint64_t* fingerprint, HybridTime* read_ht) const {
  SharedLock lock(mutex_);
  if (!versions_) {
    return false;
  }
  *versions = *versions_;
  if (fingerprint) {
    *fingerprint = fingerprint_;
  }
  if (read_ht) {
    *read_ht = read_ht_;
  }
  return true;
}

bool PgCatalogVersionsCache::GetForDb(
    uint32_t db_oid, uint64_t* catalog_version, uint64_t* last_breaking_version) const {
  SharedLock lock(mutex_);
  if (!versions_) {
    return false;
  }
  auto it = versions_->find(db_oid);
  if (it == versions_->end()) {
    return false;
  }
  if (catalog_version) {
    *catalog_version = it->second.current_version;
  }
  if (last_breaking_version) {
    *last_breaking_version = it->second.last_breaking_version;
  }
  return true;
}

DbOidVersionToMessageListMap PgCatalogVersionsCache::GetInvalMessages() const {
  SharedLock lock(mutex_);
  return inval_messages_.has_value() ? *inval_messages_ : DbOidVersionToMessageListMap();
}

bool PgCatalogVersionsCache::NeedsMessagesRefresh(uint64_t fingerprint) const {
  SharedLock lock(mutex_);
  // nullopt means the previous read of pg_yb_invalidation_messages failed, so retry it even though
  // the versions may look unchanged.
  return fingerprint_ != fingerprint || !inval_messages_.has_value();
}

}  // namespace yb::master
