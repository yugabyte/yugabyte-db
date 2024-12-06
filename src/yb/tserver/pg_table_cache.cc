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

#include "yb/tserver/pg_table_cache.h"

#include <unordered_map>

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/util/scope_exit.h"

namespace yb::tserver {

namespace {

class CacheEntry {
 public:
  CacheEntry() : future_(promise_.get_future()) {
  }

  void SetValue(const Result<client::YBTablePtr>& value) {
    promise_.set_value(value);
    has_value_.store(true);
  }

  const Result<client::YBTablePtr>& Get() const {
    return future_.get();
  }

  master::GetTableSchemaResponsePB& schema() {
    return schema_;
  }

  bool Failed() const {
    return has_value_.load() && !future_.get().ok();
  }

 private:
  std::atomic<bool> has_value_{false};
  std::promise<Result<client::YBTablePtr>> promise_;
  std::shared_future<Result<client::YBTablePtr>> future_;
  master::GetTableSchemaResponsePB schema_;
};

Result<client::YBTablePtr> CheckTableType(const Result<client::YBTablePtr>& result) {
  RETURN_NOT_OK(result);
  RSTATUS_DCHECK(
      (**result).table_type() == client::YBTableType::PGSQL_TABLE_TYPE, RuntimeError,
      "Wrong table type: $0", (**result).table_type());
  return *result;
}

using CacheEntryPtr = std::shared_ptr<CacheEntry>;

} // namespace

class PgTableCache::Impl {
 public:
  explicit Impl(std::shared_future<client::YBClient*> client_future)
      : client_future_(client_future) {}

  Status GetInfo(
      const TableId& table_id,
      master::IncludeHidden include_hidden,
      client::YBTablePtr* table,
      master::GetTableSchemaResponsePB* schema) {
    auto entry = GetEntry(table_id, include_hidden);
    const auto& table_result = entry->Get();
    RETURN_NOT_OK(table_result);
    *table = *table_result;
    *schema = entry->schema();
    return Status::OK();
  }

  Result<client::YBTablePtr> Get(const TableId& table_id) {
    return GetEntry(table_id)->Get();
  }

  void Invalidate(const TableId& table_id) {
    const auto db_oid = CHECK_RESULT(GetPgsqlDatabaseOid(table_id));
    VLOG(2) << "Invalidate table " << table_id << " in table cache of database " << db_oid;
    std::lock_guard lock(mutex_);
    auto iter = caches_.find(db_oid);
    if (iter != caches_.end()) {
      iter->second.first.erase(table_id);
    }
  }

  void InvalidateAll(CoarseTimePoint invalidation_time) {
    std::lock_guard lock(mutex_);
    if (last_cache_invalidation_ > invalidation_time) {
      return;
    }
    VLOG(2) << "Invalidate all table caches";
    last_cache_invalidation_ = CoarseMonoClock::now();
    caches_.clear();
  }

  void InvalidateDbTables(
      const std::unordered_set<uint32_t>& db_oids_updated,
      const std::unordered_set<uint32_t>& db_oids_deleted,
      CoarseTimePoint invalidation_time) {
    std::lock_guard lock(mutex_);
    for (const auto db_oid : db_oids_updated) {
      auto iter = caches_.find(db_oid);
      if (iter == caches_.end()) {
        // No need to create an empty per-database cache for db_oid just to
        // mark its "last_cache_invalidation_".
        continue;
      }
      // iter->second.second is the equivalent of last_cache_invalidation_
      // at per database level for db_oid.
      auto max_last_cache_invalidation =
        std::max(iter->second.second, last_cache_invalidation_);
      if (max_last_cache_invalidation > invalidation_time) {
        // This requested invalidation (with invalidation_time) is stale for
        // database db_oid.
        continue;
      }
      // The catalog version of database db_oid is incremented.
      VLOG(2) << "Invalidate table cache of database " << db_oid;
      iter->second.first.clear();
      iter->second.second = CoarseMonoClock::now();
    }
    for (const auto db_oid : db_oids_deleted) {
      // The database db_oid is dropped.
      VLOG(2) << "Remove table cache of dropped database " << db_oid;
      caches_.erase(db_oid);
    }
  }

 private:
  client::YBClient& client() {
    return *client_future_.get();
  }

  CacheEntryPtr GetEntry(
      const TableId& table_id,
      master::IncludeHidden include_hidden = master::IncludeHidden::kFalse) {
    auto p = DoGetEntry(table_id);
    if (p.second) {
      LoadEntry(table_id, include_hidden, p.first);
    }
    return p.first;
  }

  std::pair<CacheEntryPtr, bool> DoGetEntry(const TableId& table_id) {
    const auto db_oid = CHECK_RESULT(GetPgsqlDatabaseOid(table_id));
    std::lock_guard lock(mutex_);
    const auto iter = caches_.find(db_oid);
    auto& entry = iter != caches_.end() ? iter->second : caches_[db_oid];
    auto& cache = entry.first;
    auto it = cache.find(table_id);
    if (it != cache.end()) {
      if (it->second->Failed()) {
        it->second = std::make_shared<CacheEntry>();
        return {it->second, true};
      }
      return std::make_pair(it->second, false);
    }
    it = cache.emplace(table_id, std::make_shared<CacheEntry>()).first;
    return std::make_pair(it->second, true);
  }

  void LoadEntry(
      const TableId& table_id, master::IncludeHidden include_hidden, const CacheEntryPtr& entry) {
    auto callback = [entry](const Result<client::YBTablePtr>& result) {
      entry->SetValue(CheckTableType(result));
    };

    client().OpenTableAsync(table_id, callback, include_hidden, &entry->schema());
  }

  std::shared_future<client::YBClient*> client_future_;
  std::mutex mutex_;
  using PgTableMap = std::unordered_map<TableId, CacheEntryPtr>;
  std::unordered_map<uint32_t, std::pair<PgTableMap, CoarseTimePoint>> caches_ GUARDED_BY(mutex_);
  CoarseTimePoint last_cache_invalidation_ GUARDED_BY(mutex_);
};

PgTableCache::PgTableCache(std::shared_future<client::YBClient*> client_future)
    : impl_(new Impl(std::move(client_future))) {
}

PgTableCache::~PgTableCache() {
}

Status PgTableCache::GetInfo(
    const TableId& table_id,
    master::IncludeHidden include_hidden,
    client::YBTablePtr* table,
    master::GetTableSchemaResponsePB* schema) {
  return impl_->GetInfo(table_id, include_hidden, table, schema);
}

Result<client::YBTablePtr> PgTableCache::Get(const TableId& table_id) {
  return impl_->Get(table_id);
}

void PgTableCache::Invalidate(const TableId& table_id) {
  impl_->Invalidate(table_id);
}

void PgTableCache::InvalidateAll(CoarseTimePoint invalidation_time) {
  impl_->InvalidateAll(invalidation_time);
}

void PgTableCache::InvalidateDbTables(
    const std::unordered_set<uint32_t>& db_oids_updated,
    const std::unordered_set<uint32_t>& db_oids_deleted,
    CoarseTimePoint invalidation_time) {
  impl_->InvalidateDbTables(db_oids_updated, db_oids_deleted, invalidation_time);
}

}  // namespace yb::tserver
