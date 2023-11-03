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

#include "yb/client/meta_data_cache.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/permissions.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/roles_permissions.h"

#include "yb/util/memory/memory.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

using std::string;

DEFINE_UNKNOWN_int32(update_permissions_cache_msecs, 2000,
             "How often the roles' permissions cache should be updated. 0 means never update it");

namespace yb {
namespace client {

namespace {

Status GenerateUnauthorizedError(const std::string& canonical_resource,
                                 const ql::ObjectType& object_type,
                                 const RoleName& role_name,
                                 const PermissionType& permission,
                                 const NamespaceName& keyspace,
                                 const TableName& table) {
  switch (object_type) {
    case ql::ObjectType::TABLE:
      return STATUS_SUBSTITUTE(NotAuthorized,
          "User $0 has no $1 permission on <table $2.$3> or any of its parents",
          role_name, PermissionName(permission), keyspace, table);
    case ql::ObjectType::SCHEMA:
      if (canonical_resource == "data") {
        return STATUS_SUBSTITUTE(NotAuthorized,
            "User $0 has no $1 permission on <all keyspaces> or any of its parents",
            role_name, PermissionName(permission));
      }
      return STATUS_SUBSTITUTE(NotAuthorized,
          "User $0 has no $1 permission on <keyspace $2> or any of its parents",
          role_name, PermissionName(permission), keyspace);
    case ql::ObjectType::ROLE:
      if (canonical_resource == "role") {
        return STATUS_SUBSTITUTE(NotAuthorized,
            "User $0 has no $1 permission on <all roles> or any of its parents",
            role_name, PermissionName(permission));
      }
      return STATUS_SUBSTITUTE(NotAuthorized,
          "User $0 does not have sufficient privileges to perform the requested operation",
          role_name);
    default:
      return STATUS_SUBSTITUTE(IllegalState, "Unable to find permissions for object $0",
                               to_underlying(object_type));
  }
}

} // namespace

struct YBMetaDataCacheEntry {
  using FetchCallback = std::function<void(const Result<YBTablePtr>&)>;

  // Protects concurrent calls to YBClient::OpenTable for the entry.
  std::mutex mutex_;

  YBTablePtr table_;
  ScopedTrackedConsumption consumption_;

  std::vector<FetchCallback> callbacks;

  YBTablePtr GetFetched() {
    std::lock_guard lock(mutex_);
    return table_;
  }

  template <class Id>
  void Fetch(
      YBMetaDataCache* cache, const Id& id, const YBMetaDataCacheEntryPtr& self,
      FetchCallback callback) {
    YBTablePtr table;
    {
      std::lock_guard lock(mutex_);
      table = table_;
      if (!table) {
        bool was_empty = callbacks.empty();
        callbacks.push_back(std::move(callback));
        if (!was_empty) {
          return;
        }
      }
    }

    if (table) {
      callback(table);
      return;
    }

    PerformFetch(cache, id, self);
  }

  template <class Id>
  void PerformFetch(YBMetaDataCache* cache, const Id& id, const YBMetaDataCacheEntryPtr& self) {
    cache->client_->OpenTableAsync(id, [this, cache, self](const Result<YBTablePtr>& open_result) {
      std::vector<FetchCallback> to_notify;
      {
        std::lock_guard lock(mutex_);
        to_notify.swap(callbacks);
        if (open_result.ok()) {
          table_ = *open_result;
          if (cache->mem_tracker_) {
            consumption_ = ScopedTrackedConsumption(
                cache->mem_tracker_, table_->DynamicMemoryUsage() + sizeof(*this));
            VLOG(1) << "Consumption for table " << table_->name().ToString() << " is "
                  << consumption_.consumption() << ", memtrackerid: " << cache->mem_tracker_->id()
                  << ", schema version - " << table_->schema().version();
          }
        }
      }

      if (open_result.ok()) {
        std::lock_guard lock(cache->cached_tables_mutex_);
        const auto& table = **open_result;
        cache->cached_tables_by_name_[table.name()] = self;
        cache->cached_tables_by_id_[table.id()] = self;
      }

      for (const auto& callback : to_notify) {
        callback(open_result);
      }
    });
  }
};

YBMetaDataCache::YBMetaDataCache(
    client::YBClient* client, bool create_roles_permissions_cache, const MemTrackerPtr& mem_tracker)
    : client_(client), mem_tracker_(mem_tracker) {
  if (create_roles_permissions_cache) {
    permissions_cache_ = std::make_shared<client::internal::PermissionsCache>(client);
  } else {
    LOG(INFO) << "Creating a metadata cache without a permissions cache";
  }
}

YBMetaDataCache::~YBMetaDataCache() = default;

void YBMetaDataCache::GetTableAsync(
    const YBTableName& table_name, const GetTableAsyncCallback& callback) {
  return DoGetTableAsync(table_name, callback, &cached_tables_by_name_);
}

void YBMetaDataCache::GetTableAsync(
    const TableId& table_id, const GetTableAsyncCallback& callback) {
  return DoGetTableAsync(table_id, callback, &cached_tables_by_id_);
}

template <class Id, class Cache>
void YBMetaDataCache::DoGetTableAsync(
    const Id& id, const GetTableAsyncCallback& callback, Cache* cache) {
  std::shared_ptr<YBMetaDataCacheEntry> entry;
  YBTablePtr table;
  {
    std::lock_guard lock(cached_tables_mutex_);
    entry = cache->try_emplace(id, LazySharedPtrFactory()).first->second;
    table = entry->GetFetched();
  }

  if (table) {
    callback(GetTableResult {
      .table = table,
      .cache_used = true,
    });
    return;
  }

  entry->Fetch(this, id, entry, [callback](const auto& result) {
    if (!result.ok()) {
      callback(result.status());
    } else {
      callback(GetTableResult {
        .table = *result,
        .cache_used = false,
      });
    }
  });
}

void YBMetaDataCache::RemoveCachedTable(const YBTableName& table_name) {
  RemoveFromCache<YBTableName, TableId>(
      &cached_tables_by_name_, &cached_tables_by_id_, table_name,
      [](const std::shared_ptr<YBTable>& table) { return table->id(); },
      [](const std::shared_ptr<YBTable>& table) { return true; });
}

void YBMetaDataCache::RemoveCachedTable(const TableId& table_id, SchemaVersion schema_version) {
  RemoveFromCache<TableId, YBTableName>(
      &cached_tables_by_id_, &cached_tables_by_name_, table_id,
      [](const std::shared_ptr<YBTable>& table) { return table->name(); },
      [schema_version](const std::shared_ptr<YBTable>& table) {
        if (table->schema().version() < schema_version) {
          VLOG(1) << "Removing table " << table->name().ToString()
                  << " from cache as cached schema version " << table->schema().version()
                  << " is older than " << schema_version;
          return true;
        }
        return false;
      });
}

template <typename T, typename V, typename F, typename CanRemoveFunc>
void YBMetaDataCache::RemoveFromCache(
    std::unordered_map<T, std::shared_ptr<YBMetaDataCacheEntry>, boost::hash<T>>* direct_cache,
    std::unordered_map<V, std::shared_ptr<YBMetaDataCacheEntry>, boost::hash<V>>* indirect_cache,
    const T& direct_key,
    const F& get_indirect_key,
    const CanRemoveFunc& can_remove) {
  std::lock_guard<std::mutex> lock(cached_tables_mutex_);
  const auto itr = direct_cache->find(direct_key);
  if (itr != direct_cache->end()) {
    YBTablePtr table;
    {
      std::unique_lock<std::mutex> table_lock(itr->second->mutex_);
      if (itr->second->table_ != nullptr && can_remove(itr->second->table_)) {
        table = itr->second->table_;
      }
    }

    if (table) {
      const auto indirect_cache_key = get_indirect_key(table);
      indirect_cache->erase(indirect_cache_key);

      direct_cache->erase(itr);
    }
  }
}

void YBMetaDataCache::Reset() {
  std::lock_guard<std::mutex> lock(cached_tables_mutex_);
  cached_tables_by_id_.clear();
  cached_tables_by_name_.clear();
}

Result<std::pair<std::shared_ptr<QLType>, bool>> YBMetaDataCache::GetUDType(
    const string& keyspace_name, const string& type_name) {
  auto type_path = std::make_pair(keyspace_name, type_name);
  {
    std::lock_guard lock(cached_types_mutex_);
    auto itr = cached_types_.find(type_path);
    if (itr != cached_types_.end()) {
      return std::make_pair(itr->second, true);
    }
  }

  auto type = VERIFY_RESULT(client_->GetUDType(keyspace_name, type_name));
  {
    std::lock_guard lock(cached_types_mutex_);
    cached_types_[type_path] = type;
  }
  return std::make_pair(std::move(type), false);
}

void YBMetaDataCache::RemoveCachedUDType(const string& keyspace_name,
                                         const string& type_name) {
  std::lock_guard lock(cached_types_mutex_);
  cached_types_.erase(std::make_pair(keyspace_name, type_name));
}

Status YBMetaDataCache::WaitForPermissionCache() {
  if (!permissions_cache_) {
    LOG(WARNING) << "Permissions cache disabled. This only should be used in unit tests";
    return STATUS(TimedOut, "Permissions cache unavailable");
  }

  if (!permissions_cache_->ready()) {
    if (!permissions_cache_->WaitUntilReady(
            MonoDelta::FromMilliseconds(FLAGS_update_permissions_cache_msecs))) {
      return STATUS(TimedOut, "Permissions cache unavailable");
    }
  }
  return Status::OK();
}

Result<std::string> YBMetaDataCache::RoleSaltedHash(const RoleName& role_name) {
  RETURN_NOT_OK(WaitForPermissionCache());
  return permissions_cache_->salted_hash(role_name);
}

Result<bool> YBMetaDataCache::RoleCanLogin(const RoleName& role_name) {
  RETURN_NOT_OK(WaitForPermissionCache());
  return permissions_cache_->can_login(role_name);
}

Status YBMetaDataCache::HasResourcePermission(const std::string& canonical_resource,
                                              const ql::ObjectType& object_type,
                                              const RoleName& role_name,
                                              const PermissionType& permission,
                                              const NamespaceName& keyspace,
                                              const TableName& table,
                                              const CacheCheckMode check_mode) {
  if (!permissions_cache_) {
    LOG(WARNING) << "Permissions cache disabled. This only should be used in unit tests";
    return Status::OK();
  }

  if (object_type != ql::ObjectType::SCHEMA &&
      object_type != ql::ObjectType::TABLE &&
      object_type != ql::ObjectType::ROLE) {
    DFATAL_OR_RETURN_NOT_OK(STATUS_SUBSTITUTE(InvalidArgument, "Invalid ObjectType $0",
                                              to_underlying(object_type)));
  }

  if (!permissions_cache_->ready()) {
    if (!permissions_cache_->WaitUntilReady(
            MonoDelta::FromMilliseconds(FLAGS_update_permissions_cache_msecs))) {
      return STATUS(TimedOut, "Permissions cache unavailable");
    }
  }

  if (!permissions_cache_->HasCanonicalResourcePermission(canonical_resource, object_type,
                                                          role_name, permission)) {
    if (check_mode == CacheCheckMode::RETRY) {
      // We could have failed to find the permission because our cache is stale. If we are asked
      // to retry, we update the cache and try again.
      RETURN_NOT_OK(client_->GetPermissions(permissions_cache_.get()));
      if (permissions_cache_->HasCanonicalResourcePermission(canonical_resource, object_type,
                                                             role_name, permission)) {
        return Status::OK();
      }
    }
    return GenerateUnauthorizedError(
        canonical_resource, object_type, role_name, permission, keyspace, table);
  }

  // Found.
  return Status::OK();
}

Status YBMetaDataCache::HasTablePermission(const NamespaceName& keyspace_name,
                                           const TableName& table_name,
                                           const RoleName& role_name,
                                           const PermissionType permission,
                                           const CacheCheckMode check_mode) {

  // Check wihtout retry. In case our cache is stale, we will check again by issuing a recursive
  // call to this method.
  if (HasResourcePermission(get_canonical_keyspace(keyspace_name),
                            ql::ObjectType::SCHEMA, role_name, permission,
                            keyspace_name, "", CacheCheckMode::NO_RETRY).ok()) {
    return Status::OK();
  }

  // By default the first call asks to retry. If we decide to retry, we will issue a recursive
  // call with NO_RETRY mode.
  Status s = HasResourcePermission(get_canonical_table(keyspace_name, table_name),
                                   ql::ObjectType::TABLE, role_name, permission,
                                   keyspace_name, table_name,
                                   check_mode);

  if (check_mode == CacheCheckMode::RETRY && s.IsNotAuthorized()) {
    s = HasTablePermission(keyspace_name, table_name, role_name, permission,
                           CacheCheckMode::NO_RETRY);
  }
  return s;
}

} // namespace client
} // namespace yb
