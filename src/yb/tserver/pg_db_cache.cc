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
#include <optional>
#include <utility>
#include <vector>

#include "yb/client/client.h"

#include "yb/common/pg_types.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb::tserver {

class PgDbCache::Impl : public std::enable_shared_from_this<Impl> {
 public:
  explicit Impl(std::shared_future<client::YBClient*> client_future)
      : client_future_(std::move(client_future)) {}

  void GetColocationInfo(uint32_t db_oid, Callback callback) {
    std::optional<ColocationInfo> cached;
    bool fetcher = false;
    {
      std::lock_guard lock(mutex_);
      auto it = cache_.find(db_oid);
      if (it != cache_.end()) {
        cached = it->second;
      } else {
        auto& waiters = in_flight_[db_oid];
        fetcher = waiters.empty();
        waiters.push_back(std::move(callback));
      }
    }

    if (cached) {
      VLOG(2) << "PgDbCache hit for db " << db_oid << ": colocated=" << cached->colocated
              << " legacy_colocated_database=" << cached->legacy_colocated_database;
      callback(*cached);
      return;
    }

    if (fetcher) {
      VLOG(1) << "PgDbCache miss for db " << db_oid << "; starting master lookup";
      FetchFromMaster(db_oid);
    } else {
      VLOG(1) << "PgDbCache joining in-flight master lookup for db " << db_oid;
    }
  }

  void Invalidate(const PgTableCacheInvalidationFilter::DroppedDbs& dropped_dbs) {
    if (dropped_dbs.empty()) {
      return;
    }
    VLOG(1) << "PgDbCache removing entries for dropped db " << AsString(dropped_dbs);
    std::lock_guard lock(mutex_);
    for (auto db_oid : dropped_dbs) {
      cache_.erase(db_oid);
    }
  }

  void Clear() {
    std::lock_guard lock(mutex_);
    VLOG(1) << "PgDbCache bulk invalidation, clearing " << cache_.size() << " entries";
    cache_.clear();
  }

 private:
  void FetchFromMaster(uint32_t db_oid) {
    auto resp = std::make_shared<master::GetNamespaceInfoResponsePB>();
    client_future_.get()->GetNamespaceInfoAsync(
        GetPgsqlNamespaceId(db_oid), resp,
        [self = shared_from_this(), db_oid, resp](const Status& status) {
          self->MasterLookupDone(db_oid, status, *resp);
        });
  }

  void MasterLookupDone(
      uint32_t db_oid, const Status& status, const master::GetNamespaceInfoResponsePB& resp) {
    auto result = [&]() -> Result<ColocationInfo> {
      RETURN_NOT_OK(status);
      return ColocationInfo{
          .colocated = resp.colocated(),
          .legacy_colocated_database =
              resp.has_legacy_colocated_database() ? resp.legacy_colocated_database() : true};
    }();

    std::vector<Callback> waiters;
    {
      std::lock_guard lock(mutex_);
      if (result.ok()) {
        cache_.emplace(db_oid, *result);
        VLOG(1) << "PgDbCache populated for db " << db_oid << ": colocated=" << result->colocated
                << " legacy_colocated_database=" << result->legacy_colocated_database;
      } else {
        // The cache is left untouched on failure, so the next round of requests retries.
        VLOG(1) << "PgDbCache master lookup failed for db " << db_oid << ": " << result.status();
      }
      auto it = in_flight_.find(db_oid);
      if (it != in_flight_.end()) {
        waiters.swap(it->second);
        in_flight_.erase(it);
      }
    }

    for (auto& waiter : waiters) {
      waiter(result);
    }
  }

  std::shared_future<client::YBClient*> client_future_;
  std::mutex mutex_;

  // db oid -> db colocation info
  std::unordered_map<uint32_t, ColocationInfo> cache_;
  // db oid -> callbacks waiting on the master lookup under way for it
  std::unordered_map<uint32_t, std::vector<Callback>> in_flight_;
};

PgDbCache::PgDbCache(std::shared_future<client::YBClient*> client_future)
    : impl_(std::make_shared<Impl>(std::move(client_future))) {}

PgDbCache::~PgDbCache() = default;

void PgDbCache::GetColocationInfo(uint32_t db_oid, Callback callback) {
  impl_->GetColocationInfo(db_oid, std::move(callback));
}

void PgDbCache::Invalidate(const PgTableCacheInvalidationFilter::DroppedDbs& dropped_dbs) {
  impl_->Invalidate(dropped_dbs);
}

void PgDbCache::Clear() { impl_->Clear(); }

}  // namespace yb::tserver
