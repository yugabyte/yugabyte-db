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

#pragma once

#include <future>
#include <span>

#include <boost/container/small_vector.hpp>

#include "yb/client/client_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_ddl.fwd.h"

#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"

namespace yb::tserver {

struct PgTableCacheGetOptions {
  bool reopen = false;
  uint64_t min_ysql_catalog_version = 0;
  master::IncludeHidden include_hidden = master::IncludeHidden::kFalse;
};

void AddTableIdIfMissing(
    const TableId& table_id, boost::container::small_vector_base<TableId>& table_ids);

class PgTablesQueryResult;

class PgTablesQueryListener {
 public:
  virtual ~PgTablesQueryListener() = default;
  virtual void Ready() = 0;
};
using PgTablesQueryListenerPtr = std::shared_ptr<PgTablesQueryListener>;

class PgTablesQueryResultBuilder;

class PgTablesQueryResult {
 public:
  struct TableInfo {
    client::YBTablePtr table;
    std::shared_ptr<master::GetTableSchemaResponsePB> schema;

    std::string ToString() const;
  };

  PgTablesQueryResult() = default;

  Result<client::YBTablePtr> Get(const TableId& table_id) const;
  Result<TableInfo> GetInfo(const TableId& table_id) const;

 private:
  friend class PgTablesQueryResultBuilder;

  std::mutex mutex_;
  Status failure_status_ GUARDED_BY(mutex_);
  size_t pending_tables_ GUARDED_BY(mutex_) = 0;
  boost::container::small_vector<TableInfo, 4> tables_ GUARDED_BY(mutex_);
};

using PgTablesQueryResultPtr = std::shared_ptr<PgTablesQueryResult>;

class PgTableCache {
 public:
  explicit PgTableCache(std::shared_future<client::YBClient*> client_future);
  ~PgTableCache();

  Status GetInfo(
      const TableId& table_id,
      const PgTableCacheGetOptions& options,
      client::YBTablePtr* table,
      master::GetTableSchemaResponsePB* schema);

  Result<client::YBTablePtr> Get(const TableId& table_id);
  void GetTables(
      std::span<const TableId> table_ids,
      const PgTableCacheGetOptions& options,
      const PgTablesQueryResultPtr& result,
      const PgTablesQueryListenerPtr& listener);

  void Invalidate(const TableId& table_id);
  void InvalidateAll(CoarseTimePoint invalidation_time);
  void InvalidateDbTables(const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
                          const std::unordered_set<uint32_t>& db_oids_deleted);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
