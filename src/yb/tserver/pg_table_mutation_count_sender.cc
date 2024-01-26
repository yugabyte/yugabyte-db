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

#include "yb/tserver/pg_table_mutation_count_sender.h"

#include <chrono>

#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/unique_lock.h"

DECLARE_int32(yb_client_admin_operation_timeout_sec);

using namespace std::chrono_literals;

DEFINE_RUNTIME_uint64(ysql_node_level_mutation_reporting_interval_ms, 5 * 1000,
                      "Interval at which the node level table mutation counts are sent to the auto "
                      "analyze service which tracks table mutation counters at the cluster level.");
DEFINE_RUNTIME_int32(ysql_node_level_mutation_reporting_timeout_ms, 5 * 1000,
                      "Timeout for mutation reporting rpc to auto-analyze service.");

namespace yb {

namespace tserver {

TableMutationCountSender::TableMutationCountSender(TabletServer* server)
    : server_(*server) {
}

TableMutationCountSender::~TableMutationCountSender() {
  DCHECK(!thread_) << "Stop should be called before destruction";
}

Status TableMutationCountSender::Start() {
  RSTATUS_DCHECK(!thread_, InternalError, "Table mutation sender thread already exists");
  VLOG(1) << "Initializing table mutation count sender thread";
  return yb::Thread::Create("pg_table_mutation_count_sender", "table_mutation_count_send",
      &TableMutationCountSender::RunThread, this, &thread_);
}

Status TableMutationCountSender::Stop() {
  if (!thread_) {
    return Status::OK();
  }

  {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    cond_.notify_one();
  }

  decltype(thread_) thread;
  thread.swap(thread_);
  return ThreadJoiner(thread.get()).Join();
}

Status TableMutationCountSender::DoSendMutationCounts() {
  // Send mutations to the auto analyze stateful service that aggregates mutations from all nodes
  // and triggers ANALYZE as necessary.
  auto mutation_counts = server_.GetPgNodeLevelMutationCounter().GetAndClear();
  if (mutation_counts.size() == 0) {
    VLOG(4) << "No table mutation counts to send";
    return Status::OK();
  }

  if (!client_) {
    auto client = std::make_unique<client::PgAutoAnalyzeServiceClient>();
    RETURN_NOT_OK(client->Init(&server_));
    client_.swap(client);
  }

  stateful_service::IncreaseMutationCountersRequestPB req;

  for (const auto& [table_id, mutation_count] : mutation_counts) {
    auto table_mutation_count = req.add_table_mutation_counts();
    table_mutation_count->set_table_id(table_id);
    table_mutation_count->set_mutation_count(mutation_count);
  }

  VLOG(3) << "Sending table mutation counts: " << req.ShortDebugString();

  const auto result = client_->IncreaseMutationCounters(
      req, GetAtomicFlag(&FLAGS_ysql_node_level_mutation_reporting_timeout_ms) * 1ms);
  if (!result) {
    VLOG(2) << "Result: " << result.ToString();

    // It is possible that cluster-level mutation counters were updated but the response failed for
    // some other reason. In this case, unless we are certain that the cluster-level mutation
    // counters weren't updated, we avoid retrying to avoid double counting (i.e., we prefer
    // avoiding over-aggresive auto ANALYZE).
    if (result.status().IsTryAgain()) {
      for (const auto& [table_id, mutation_count] : mutation_counts) {
        server_.GetPgNodeLevelMutationCounter().Increase(table_id, mutation_count);
      }
    }
    return result.status();
  }

  // TODO: Handle per-table errors. Same as above, retry later if we are certain that the
  // cluster-level mutation counters weren't updated.

  return Status::OK();
}

void TableMutationCountSender::RunThread() {
  while (true) {
    {
      UniqueLock lock(mutex_);
      VLOG(5) << "Next send after "
              << GetAtomicFlag(&FLAGS_ysql_node_level_mutation_reporting_interval_ms) << "ms";
      cond_.wait_for(GetLockForCondition(&lock),
                     GetAtomicFlag(&FLAGS_ysql_node_level_mutation_reporting_interval_ms) * 1ms);

      if (stopped_) {
        VLOG(1) << "Table mutation count sender thread has finished";
        return;
      }
    }
    WARN_NOT_OK(DoSendMutationCounts(), "Failed to send table mutation counts");
  }
}


} // namespace tserver
} // namespace yb
