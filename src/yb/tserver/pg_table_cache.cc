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

#include "yb/tserver/pg_table_cache.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/util/scope_exit.h"
#include "yb/util/std_util.h"

namespace yb::tserver {
namespace {

class TablesResultBuilder : public std::enable_shared_from_this<TablesResultBuilder> {
  using TableInfo = PgTablesQueryResult::TableInfo;
  using TablesResult = PgTablesQueryResult::TablesResult;
  using TablesResultPtr = PgTablesQueryResult::TablesResultPtr;

  class PrivateTag {};

 public:
  TablesResultBuilder(size_t expected_tables_count, PrivateTag)
      : expected_tables_count_(expected_tables_count) {
    tables_.reserve(expected_tables_count);
  }

  [[nodiscard]] TablesResultPtr CompleteResultOnce(const Result<TableInfo>& table) {
    std::lock_guard lock(mutex_);
    // Current method is called multiple times (once for each requested table).
    // In case result for at least one table is not OK same status is placed into
    // completed_result_holder_ immediately (non null pointer is returned in this case,
    // because the build process is completed).
    // As a result it is possible that current method will be called when completed_result_holder_
    // has value. It is required to return nullptr in this case.
    if (completed_result_holder_) {
      return nullptr;
    }
    if (!table.ok()) {
      completed_result_holder_.emplace(table.status());
    } else {
      tables_.push_back(*table);
      if (tables_.size() < expected_tables_count_) {
        return nullptr;
      }
      completed_result_holder_.emplace(std::span{tables_});
    }
    DCHECK(completed_result_holder_.has_value());
    return SharedField(shared_from_this(), &*completed_result_holder_);
  }

  [[nodiscard]] static auto Make(size_t expected_tables_count) {
    return std::make_shared<TablesResultBuilder>(expected_tables_count, PrivateTag{});
  }

 private:
  const size_t expected_tables_count_;
  std::mutex mutex_;
  boost::container::small_vector<TableInfo, 4> tables_ GUARDED_BY(mutex_);
  std::optional<TablesResult> completed_result_holder_ GUARDED_BY(mutex_);
};

class Waiter {
  using TableInfo = PgTablesQueryResult::TableInfo;

 public:
  Waiter(size_t expected_tables_count, const PgTablesQueryListenerPtr& listener)
      : builder_(TablesResultBuilder::Make(expected_tables_count)),
        listener_(listener) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "pending tables: " << expected_tables_count;
  }

  void TableReady(const Result<TableInfo>& table_info) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "info: " << AsString(table_info);
    if (auto tables_result_ptr = builder_->CompleteResultOnce(table_info)) {
      VLOG_WITH_PREFIX(4) << "notify listener";
      listener_->Ready(PgTablesQueryResult{std::move(tables_result_ptr)});
    }
  }

 private:
  const void* LogPrefix() const {
    return builder_.get();
  }

  const std::shared_ptr<TablesResultBuilder> builder_;
  const PgTablesQueryListenerPtr listener_;
};

class CacheEntry : public std::enable_shared_from_this<CacheEntry> {
 public:
  CacheEntry() : future_(promise_.get_future()) {
  }

  void SetValue(const Result<client::YBTablePtr>& value) {
    promise_.set_value(value);

    Waiters waiters;
    {
      std::lock_guard lock(mutex_);
      has_value_.store(true);
      waiters.swap(waiters_);
    }
    if (waiters.empty()) {
      return;
    }
    auto table_info = MakeTableInfo(value);
    for (auto& waiter : waiters) {
      waiter.TableReady(table_info);
    }
  }

  void Wait(Waiter waiter) {
    {
      std::lock_guard lock(mutex_);
      if (!has_value_.load(std::memory_order_relaxed)) {
        waiters_.push_back(std::move(waiter));
        return;
      }
    }
    waiter.TableReady(MakeTableInfo(future_.get()));
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
  using Waiters = std::vector<Waiter>;
  Waiters waiters_ GUARDED_BY(mutex_);
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

std::string PgTablesQueryResult::TableInfo::ToString() const {
  return YB_STRUCT_TO_STRING(table, schema);
}

class PgTableCache::Impl {
 public:
  explicit Impl(std::shared_future<client::YBClient*> client_future)
      : client_future_(std::move(client_future)) {}

  Result<client::YBTablePtr> Get(TableIdView table_id) {
    return GetEntry(table_id)->Get();
  }

  void GetTables(
      std::span<const TableId> table_ids,
      const PgTablesQueryListenerPtr& listener,
      const PgTableCacheGetOptions& options) {
    boost::container::small_vector<std::pair<const TableId*, CacheEntryPtr>, 8> entries;
    {
      std::lock_guard lock(mutex_);
      for (const auto& table_id : table_ids) {
        auto [entry, need_load] = DoGetEntryUnlocked(table_id, options);
        entries.emplace_back(need_load ? &table_id : nullptr, entry);
      }
    }
    const Waiter waiter{table_ids.size(), listener};
    for (const auto& [table_id, entry] : entries) {
      entry->Wait(waiter);
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
      TableIdView table_id,
      const PgTableCacheGetOptions& options = {}) {
    auto p = DoGetEntry(table_id, options);
    if (p.second) {
      LoadEntry(table_id, options.include_hidden, p.first);
    }
    return p.first;
  }

  std::pair<CacheEntryPtr, bool> DoGetEntry(
      TableIdView table_id, const PgTableCacheGetOptions& options) {
    std::lock_guard lock(mutex_);
    return DoGetEntryUnlocked(table_id, options);
  }

  std::pair<CacheEntryPtr, bool> DoGetEntryUnlocked(
      TableIdView table_id, const PgTableCacheGetOptions& options) REQUIRES(mutex_) {
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
      TableIdView table_id, master::IncludeHidden include_hidden, const CacheEntryPtr& entry) {
    auto callback = [entry, table_id](const Result<client::YBTablePtr>& result) {
      VLOG(4)
          << "PG table cache entry response for " << table_id << ": "
          << (result.ok() ? (*result)->ToString() : result.status().ToString());
      entry->SetValue(CheckTableType(result));
    };

    VLOG(4) << "PG table cache fetching table schema for table " << table_id
            << " include_hidden = " << include_hidden;
    client().OpenTableAsync(table_id, callback, include_hidden, &entry->schema());
  }

  std::shared_future<client::YBClient*> client_future_;
  std::mutex mutex_;
  using PgTableMap = UnorderedStringMap<TableId, CacheEntryPtr>;
  // db_id -> (table_id -> table_cache_entry, catalog_version)
  std::unordered_map<uint32_t, std::pair<PgTableMap, uint64_t>> caches_ GUARDED_BY(mutex_);
  CoarseTimePoint last_cache_invalidation_ GUARDED_BY(mutex_);
};

PgTableCache::PgTableCache(std::shared_future<client::YBClient*> client_future)
    : impl_(new Impl(std::move(client_future))) {
}

PgTableCache::~PgTableCache() = default;

Result<client::YBTablePtr> PgTableCache::Get(TableIdView table_id) {
  return impl_->Get(table_id);
}

void PgTableCache::GetTables(
    std::span<const TableId> table_ids,
    const PgTablesQueryListenerPtr& listener,
    const PgTableCacheGetOptions& options) {
  return impl_->GetTables(table_ids, listener, options);
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

Result<const client::YBTablePtr&> PgTablesQueryResult::Get(TableIdView table_id) const {
  return VERIFY_RESULT_REF(GetInfo(table_id)).table;
}

Result<const PgTablesQueryResult::TableInfo&> PgTablesQueryResult::GetInfo(
    TableIdView table_id) const {
  RETURN_NOT_OK(*tables_);
  const auto& tables = **tables_;
  const auto it = std::ranges::find_if(
      tables, [&table_id](const auto& table_info) { return table_info.table->id() == table_id; });
  SCHECK(it != tables.end(), InvalidArgument, "Table $0 not found in query", table_id);
  return *it;
}

}  // namespace yb::tserver
