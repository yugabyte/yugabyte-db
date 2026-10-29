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

#include <future>
#include <memory>
#include <span>
#include <string>

#include <boost/container/container_fwd.hpp>

#include "yb/client/client_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_ddl.fwd.h"

#include "yb/tserver/pg_cache_util.h"
#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/thread_pool.h"

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

  PgTablesQueryResult() = default;
  explicit PgTablesQueryResult(TablesResultPtr&& tables) : tables_(std::move(tables)) {}

  Result<const client::YBTablePtr&> Get(TableIdView table_id) const;
  Result<const TableInfo&> GetInfo(TableIdView table_id) const;

 private:
  TablesResultPtr tables_;
};

class PgTableCache {
 public:
  class Listener {
   public:
    struct BlockingCallExecContext {
      const std::shared_ptr<Listener> self;
      YBThreadPool& pool;
    };

    virtual ~Listener() = default;

    virtual void ResultReady(
        PgTablesQueryResult&& result, const BlockingCallExecContext* blocking_call_exec_ctx) = 0;
  };

  using ListenerPtr = std::shared_ptr<Listener>;

  PgTableCache(std::shared_future<client::YBClient*> client_future, YBThreadPool& pool);
  ~PgTableCache();

  Result<client::YBTablePtr> Get(TableIdView table_id);
  // Cache-only lookup under one lock acquisition; appends one entry per id, nullptr unless the
  // table is already loaded. Never fetches from master.
  void GetIfCached(
      std::span<const TableId> table_ids,
      boost::container::small_vector_base<client::YBTablePtr>& tables);
  void GetTables(
      std::span<const TableId> table_ids, const ListenerPtr& listener,
      const PgTableCacheGetOptions& options = {});

  Result<PgTablesQueryResult> GetTables(
      std::span<const TableId> table_ids, CoarseTimePoint deadline,
      const PgTableCacheGetOptions& options = {});

  void Invalidate(const TableId& table_id);
  void InvalidateAll(CoarseTimePoint invalidation_time);
  void InvalidateDbTables(const PgTableCacheInvalidationFilter& filter);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

class PgTablesQueryListenerNonBlocking : public PgTableCache::Listener {
 public:
  virtual void ReadyNonBlocking(PgTablesQueryResult&& result) = 0;

 private:
  void ResultReady(PgTablesQueryResult&& result, const BlockingCallExecContext*) override final;
};

class PgTablesQueryListener : public PgTableCache::Listener, public ThreadPoolTask {
 public:
  virtual void Ready(PgTablesQueryResult&& result) = 0;

 private:
  void Run() override final;
  void Done(const Status& status) override final;

  void ResultReady(
      PgTablesQueryResult&& result,
      const BlockingCallExecContext* blocking_call_exec_ctx) override final;

  std::shared_ptr<Listener> retained_self_;
  PgTablesQueryResult result_holder_;
};

} // namespace yb::tserver
