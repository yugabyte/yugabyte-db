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

#include "yb/tserver/pg_db_cache.h"

#include <mutex>
#include <utility>

#include "yb/client/client.h"

#include "yb/common/pg_types.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb::tserver {

class PgDbCache::Impl {
 public:
  explicit Impl(std::shared_future<client::YBClient*> client_future)
      : client_future_(std::move(client_future)) {}

  Result<ColocationInfo> GetColocationInfo(uint32_t db_oid) {
    std::shared_future<Result<ColocationInfo>> future;
    bool fetcher = false;
    std::promise<Result<ColocationInfo>> promise;

    {
      // Cache hit
      std::lock_guard lock(mutex_);
      auto cached = cache_.find(db_oid);
      if (cached != cache_.end()) {
        VLOG(2) << "PgDbCache hit for db " << db_oid << ": colocated=" << cached->second.colocated
                << " legacy_colocated_database=" << cached->second.legacy_colocated_database;
        return cached->second;
      }

      // Cache miss: join queue of pending requests for this db.
      auto in_flight = in_flight_.find(db_oid);
      if (in_flight != in_flight_.end()) {
        VLOG(1) << "PgDbCache joining in-flight master lookup for db " << db_oid;
        future = in_flight->second;
      } else {
        // Cache miss: we are the fetcher for this db oid
        VLOG(1) << "PgDbCache miss for db " << db_oid << "; starting master lookup";
        fetcher = true;
        future = promise.get_future().share();
        in_flight_.emplace(db_oid, future);
      }
    }

    if (fetcher) {
      auto result = FetchFromMaster(db_oid);
      {
        std::lock_guard lock(mutex_);
        if (result.ok()) {
          // Cache miss filled succesfully.
          cache_.emplace(db_oid, *result);
          VLOG(1) << "PgDbCache populated for db " << db_oid << ": colocated=" << result->colocated
                  << " legacy_colocated_database=" << result->legacy_colocated_database;
        } else {
          // Cache miss: On failure, the current set of waiters get the failure
          // but the cache is not updated, so the next round of requests will
          // retry.
          VLOG(1) << "PgDbCache master lookup failed for db " << db_oid << ": " << result.status();
        }
        in_flight_.erase(db_oid);
      }
      // Cache miss: Set the result for concurrent pending requests for this db.
      promise.set_value(std::move(result));
    }

    return future.get();
  }

  void Invalidate(const std::unordered_set<uint32_t>& db_oids_deleted) {
    if (db_oids_deleted.empty()) {
      return;
    }
    VLOG(1) << "PgDbCache removing entries for dropped db " << yb::ToString(db_oids_deleted);
    std::lock_guard lock(mutex_);
    for (auto db_oid : db_oids_deleted) {
      cache_.erase(db_oid);
    }
  }

  void Clear() {
    std::lock_guard lock(mutex_);
    VLOG(1) << "PgDbCache bulk invalidation, clearing " << cache_.size() << " entries";
    cache_.clear();
  }

 private:
  Result<ColocationInfo> FetchFromMaster(uint32_t db_oid) {
    auto* client = client_future_.get();
    master::GetNamespaceInfoResponsePB resp;
    RETURN_NOT_OK(client->GetNamespaceInfo(GetPgsqlNamespaceId(db_oid), &resp));
    ColocationInfo info;
    info.colocated = resp.colocated();
    info.legacy_colocated_database =
        resp.has_legacy_colocated_database() ? resp.legacy_colocated_database() : true;
    return info;
  }

  std::shared_future<client::YBClient*> client_future_;
  std::mutex mutex_;

  // db oid -> db colocation info
  std::unordered_map<uint32_t, ColocationInfo> cache_;
  // db oid -> current future for pending requests
  std::unordered_map<uint32_t, std::shared_future<Result<ColocationInfo>>> in_flight_;
};

PgDbCache::PgDbCache(std::shared_future<client::YBClient*> client_future)
    : impl_(std::make_unique<Impl>(std::move(client_future))) {}

PgDbCache::~PgDbCache() = default;

Result<PgDbCache::ColocationInfo> PgDbCache::GetColocationInfo(uint32_t db_oid) {
  return impl_->GetColocationInfo(db_oid);
}

void PgDbCache::Invalidate(const std::unordered_set<uint32_t>& db_oids_deleted) {
  impl_->Invalidate(db_oids_deleted);
}

void PgDbCache::Clear() { impl_->Clear(); }

}  // namespace yb::tserver
