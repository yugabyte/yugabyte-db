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

class PgTablesQueryResultBuilder {
 public:
  PgTablesQueryResultBuilder(PgTablesQueryResult& result, size_t size) : result_(result) {
    result.pending_tables_ = size;
    result.tables_.reserve(size);
  }

  bool TableReady(const Result<PgTablesQueryResult::TableInfo>& info) const {
    std::lock_guard lock(result_.mutex_);
    if (!result_.failure_status_.ok()) {
      return false;
    }
    if (!info.ok()) {
      result_.failure_status_ = info.status();
      return true;
    }

    result_.tables_.push_back(*info);
    return --result_.pending_tables_ == 0;
  }

 private:
  PgTablesQueryResult& result_;
};

namespace {

class CacheEntry : public std::enable_shared_from_this<CacheEntry> {
 public:
  CacheEntry() : future_(promise_.get_future()) {
  }

  void SetValue(const Result<client::YBTablePtr>& value) {
    promise_.set_value(value);

    decltype(waiters_) queries;
    {
      std::lock_guard lock(mutex_);
      has_value_.store(true);
      queries = std::move(waiters_);
    }
    if (!queries.empty()) {
      auto table_info = MakeTableInfo(value);
      for (const auto& [result, listener] : queries) {
        if (result.TableReady(table_info)) {
          listener->Ready();
        }
      }
    }
  }

  void Wait(PgTablesQueryResultBuilder result, const PgTablesQueryListenerPtr& listener) {
    {
      std::lock_guard lock(mutex_);
      if (!has_value_.load(std::memory_order_relaxed)) {
        waiters_.emplace_back(result, listener);
        return;
      }
    }
    if (result.TableReady(MakeTableInfo(future_.get()))) {
      listener->Ready();
    }
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
  Result<PgTablesQueryResult::TableInfo> MakeTableInfo(const Result<client::YBTablePtr>& value) {
    RETURN_NOT_OK(value);
    return PgTablesQueryResult::TableInfo {
      .table = *value,
      .schema = std::shared_ptr<master::GetTableSchemaResponsePB>(
        shared_from_this(), &schema_)
    };
  }

  std::atomic<bool> has_value_{false};
  std::promise<Result<client::YBTablePtr>> promise_;
  std::shared_future<Result<client::YBTablePtr>> future_;
  master::GetTableSchemaResponsePB schema_;

  std::mutex mutex_;
  std::vector<
      std::pair<PgTablesQueryResultBuilder, PgTablesQueryListenerPtr>> waiters_ GUARDED_BY(mutex_);
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
      : client_future_(std::move(client_future)) {}

  Status GetInfo(
    const TableId& table_id,
    const PgTableCacheGetOptions& options,
    client::YBTablePtr* table,
    master::GetTableSchemaResponsePB* schema) {
  auto entry = GetEntry(table_id, options);
  const auto& table_result = entry->Get();
  RETURN_NOT_OK(table_result);
  *table = *table_result;
  *schema = entry->schema();
  return Status::OK();
}

  Result<client::YBTablePtr> Get(const TableId& table_id) {
    PgTableCacheGetOptions default_options;
    return GetEntry(table_id, default_options)->Get();
  }

  void GetTables(
      std::span<const TableId> table_ids,
      const PgTableCacheGetOptions& options,
      PgTablesQueryResult& result,
      const PgTablesQueryListenerPtr& listener) {
    boost::container::small_vector<std::pair<const TableId*, CacheEntryPtr>, 8> entries;
    {
      std::lock_guard lock(mutex_);
      for (const auto& table_id : table_ids) {
        auto [entry, need_load] = DoGetEntryUnlocked(table_id, options);
        entries.emplace_back(need_load ? &table_id : nullptr, entry);
      }
    }
    PgTablesQueryResultBuilder builder(result, table_ids.size());
    for (const auto& [table_id, entry] : entries) {
      entry->Wait(builder, listener);
      if (table_id) {
        LoadEntry(*table_id, options.include_hidden, entry);
      }
    }
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
      const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
      const std::unordered_set<uint32_t>& db_oids_deleted) {
    std::lock_guard lock(mutex_);
    for (auto [db_oid, db_version] : db_oids_updated) {
      auto iter = caches_.find(db_oid);
      if (iter == caches_.end()) {
        // No need to create an empty per-database cache for db_oid just to
        // mark its "last_cache_invalidation_".
        continue;
      }
      if (iter->second.second >= db_version) {
        VLOG(5) << "Skipping invalidation of table cache for db id " << db_oid
        << " new version is " << db_version
        << " existing version is " << iter->second.second;
        continue;
      }

      VLOG(1) << "Invalidating entire table cache of database " << db_oid
      << " due to an out of date cache catalog version " << iter->second.second
      << " compared to latest version " << db_version;
      iter->second.first.clear();
      iter->second.second = db_version;
    }
    for (auto db_oid : db_oids_deleted) {
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
      const PgTableCacheGetOptions& options) {
    auto p = DoGetEntry(table_id, options);
    if (p.second) {
      LoadEntry(table_id, options.include_hidden, p.first);
    }
    return p.first;
  }

  std::pair<CacheEntryPtr, bool> DoGetEntry(
      const TableId& table_id, const PgTableCacheGetOptions& options) {
    std::lock_guard lock(mutex_);
    return DoGetEntryUnlocked(table_id, options);
  }

  std::pair<CacheEntryPtr, bool> DoGetEntryUnlocked(
      const TableId& table_id, const PgTableCacheGetOptions& options) REQUIRES(mutex_) {
    const auto db_oid = CHECK_RESULT(GetPgsqlDatabaseOid(table_id));
    const auto iter = caches_.find(db_oid);

    auto& db_entry = iter != caches_.end() ? iter->second : caches_[db_oid];
    if (db_entry.second < options.min_ysql_catalog_version) {
      VLOG(1) << "Get: invalidating entire table cache of database " << db_oid
      << " due to an out of date cache catalog version " << db_entry.second
      << " compared to latest version " << options.min_ysql_catalog_version;
      db_entry.first.clear();
      db_entry.second = options.min_ysql_catalog_version;
    }
    auto& db_cache = db_entry.first;
    auto it = db_cache.find(table_id);
    if (it != db_cache.end()) {
      if (options.reopen) {
        VLOG(2) << "Forcing table cache invalidation for table " << table_id;
        it->second = std::make_shared<CacheEntry>();
        return {it->second, true};
      }
      if (it->second->Failed()) {
        VLOG(1) << "PG table cache unavailable for table " << table_id;
        it->second = std::make_shared<CacheEntry>();
        return {it->second, true};
      }
      return std::make_pair(it->second, false);
    }
    it = db_cache.emplace(table_id, std::make_shared<CacheEntry>()).first;
    VLOG(5) << "PG table cache miss for table " << table_id;
    return {it->second, true};
  }

  void LoadEntry(
      const TableId& table_id, master::IncludeHidden include_hidden, const CacheEntryPtr& entry) {
    auto callback = [entry](const Result<client::YBTablePtr>& result) {
      entry->SetValue(CheckTableType(result));
      VLOG_IF(4, result.ok()) << "Set PG table cache entry for table " << (*result)->id();
    };

    VLOG(4) << "PG table cache fetching table schema for table " << table_id
            << " include_hidden = " << include_hidden;
    client().OpenTableAsync(table_id, callback, include_hidden, &entry->schema());
  }

  std::shared_future<client::YBClient*> client_future_;
  std::mutex mutex_;
  using PgTableMap = std::unordered_map<TableId, CacheEntryPtr>;
  // db_id -> (table_id -> table_cache_entry, catalog_version)
  std::unordered_map<uint32_t, std::pair<PgTableMap, uint64_t>> caches_ GUARDED_BY(mutex_);
  CoarseTimePoint last_cache_invalidation_ GUARDED_BY(mutex_);
};

PgTableCache::PgTableCache(std::shared_future<client::YBClient*> client_future)
    : impl_(new Impl(std::move(client_future))) {
}

PgTableCache::~PgTableCache() = default;

Status PgTableCache::GetInfo(
    const TableId& table_id,
    const PgTableCacheGetOptions& options,
    client::YBTablePtr* table,
    master::GetTableSchemaResponsePB* schema) {
  return impl_->GetInfo(table_id, options, table, schema);
}

Result<client::YBTablePtr> PgTableCache::Get(const TableId& table_id) {
  return impl_->Get(table_id);
}

void PgTableCache::GetTables(
    std::span<const TableId> table_ids,
    const PgTableCacheGetOptions& options,
    PgTablesQueryResult& result,
    const PgTablesQueryListenerPtr& listener) {
  return impl_->GetTables(table_ids, options, result, listener);
}

void PgTableCache::Invalidate(const TableId& table_id) {
  impl_->Invalidate(table_id);
}

void PgTableCache::InvalidateAll(CoarseTimePoint invalidation_time) {
  impl_->InvalidateAll(invalidation_time);
}

void PgTableCache::InvalidateDbTables(
    const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
    const std::unordered_set<uint32_t>& db_oids_deleted) {
  impl_->InvalidateDbTables(db_oids_updated, db_oids_deleted);
}

void AddTableIdIfMissing(
    const TableId& table_id, boost::container::small_vector_base<TableId>& table_ids) {
  if (std::ranges::find(table_ids, table_id) == table_ids.end()) {
    table_ids.push_back(table_id);
  }
}

Result<client::YBTablePtr> PgTablesQueryResult::Get(const TableId& table_id) const {
  return VERIFY_RESULT(GetInfo(table_id)).table;
}

Result<PgTablesQueryResult::TableInfo> PgTablesQueryResult::GetInfo(
    const TableId& table_id) const NO_THREAD_SAFETY_ANALYSIS {
  RETURN_NOT_OK(failure_status_);
  for (const auto& table_info : tables_) {
    if (table_info.table->id() == table_id) {
      return table_info;
    }
  }
  return STATUS_FORMAT(InvalidArgument, "Table $0 not found in query", table_id);
}

}  // namespace yb::tserver
