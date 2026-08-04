//--------------------------------------------------------------------------------------------------
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
//
// SystemQueryCache proactively caches system queries to be used by
// CQLProcessors.  This helps with performance and availability, since masters
// have reduced traffic when a connection is established.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <condition_variable>
#include <unordered_map>

#include "yb/gutil/thread_annotations.h"

#include "yb/rpc/io_thread_pool.h"

#include "yb/util/condition_variable.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"

#include "yb/yql/cql/ql/util/statement_params.h"
#include "yb/yql/cql/ql/util/statement_result.h"

namespace chrono = std::chrono;

namespace yb {

namespace cqlserver {
class CQLServiceImpl;
}

namespace cqlserver {

using ql::RowsResult;
using ql::ExecutedResult;

class SystemQueryCache {
 public:
  explicit SystemQueryCache(cqlserver::CQLServiceImpl* service_impl);
  ~SystemQueryCache();

  std::optional<RowsResult::SharedPtr> Lookup(const std::string& query);

  MonoDelta GetStaleness();

  void Shutdown();

 private:
  void InitializeQueries();
  void RefreshCache();
  void ScheduleRefreshCache(bool now);
  Result<ExecutedResult::SharedPtr> ExecuteSync(const std::string& stmt);

  struct CallbackState {
    Status status;
    ExecutedResult::SharedPtr result;
    bool callback_done;

    CallbackState() : callback_done(false) {}
  };

  void ExecuteSyncCallback(
      std::shared_ptr<CallbackState> state, const Status& status,
      const ExecutedResult::SharedPtr& result);

  cqlserver::CQLServiceImpl* const service_impl_;
  std::vector<std::string> queries_;

  std::unique_ptr<std::unordered_map<std::string, RowsResult::SharedPtr>> cache_
      GUARDED_BY(cache_mutex_);
  MonoTime last_updated_ GUARDED_BY(cache_mutex_);
  std::mutex cache_mutex_;
  Mutex request_mutex_;
  ConditionVariable request_cv_{&request_mutex_};

  // Required for executing statements
  ql::StatementParameters stmt_params_;

  // Thread pool used by the scheduler.
  std::unique_ptr<yb::rpc::IoThreadPool> pool_;
  // The scheduler used to refresh the system queries.
  std::unique_ptr<yb::rpc::Scheduler> scheduler_;
  bool shutting_down_ GUARDED_BY (request_mutex_);
};

} // namespace cqlserver
} // namespace yb
