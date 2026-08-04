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

#include <memory>
#include <future>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

class PgTablesQueryResult {
 public:
  struct TableInfo {
    client::YBTablePtr table;
    std::shared_ptr<master::GetTableSchemaResponsePB> schema;

    std::string ToString() const;
  };

  using Tables = std::span<const TableInfo>;
  using TablesResult = Result<std::span<const TableInfo>>;
  using TablesResultPtr = std::shared_ptr<TablesResult>;

  explicit PgTablesQueryResult(TablesResultPtr&& tables) : tables_(std::move(tables)) {}

  Result<const client::YBTablePtr&> Get(TableIdView table_id) const;
  Result<const TableInfo&> GetInfo(TableIdView table_id) const;

 private:
  TablesResultPtr tables_;
};

class PgTablesQueryListener {
 public:
  virtual ~PgTablesQueryListener() = default;
  virtual void Ready(const PgTablesQueryResult& result) = 0;
};
using PgTablesQueryListenerPtr = std::shared_ptr<PgTablesQueryListener>;

class PgTableCache {
 public:
  explicit PgTableCache(std::shared_future<client::YBClient*> client_future);
  ~PgTableCache();

  Result<client::YBTablePtr> Get(TableIdView table_id);
  void GetTables(
      std::span<const TableId> table_ids,
      const PgTablesQueryListenerPtr& listener,
      const PgTableCacheGetOptions& options = {});

  void Invalidate(const TableId& table_id);
  void InvalidateAll(CoarseTimePoint invalidation_time);
  void InvalidateDbTables(const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
                          const std::unordered_set<uint32_t>& db_oids_deleted);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
