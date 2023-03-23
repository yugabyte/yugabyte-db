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

#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/unique_lock.h"

DECLARE_int32(yb_client_admin_operation_timeout_sec);

using namespace std::literals;

DEFINE_RUNTIME_uint64(ysql_table_mutation_count_aggregate_interval_ms, 5 * 1000,
                      "Interval at which the table mutation counts are sent to the auto analyze "
                      "service which tracks table mutation counters at the cluster level.");

namespace yb {

namespace tserver {

TableMutationCountSender::TableMutationCountSender(TabletServer* server)
    : server_(server) {
}

TableMutationCountSender::~TableMutationCountSender() {
  DCHECK(!should_run_) << "Stop should be called before destruction";
}

Status TableMutationCountSender::Start() {
  RSTATUS_DCHECK(thread_ == nullptr, InternalError, "Table mutation sender thread already exists");

  std::lock_guard lock(mutex_);
  should_run_ = true;
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
    should_run_ = false;
    cond_.notify_one();
  }

  RETURN_NOT_OK(ThreadJoiner(thread_.get()).Join());
  thread_ = nullptr;
  return Status::OK();
}

Status TableMutationCountSender::DoSendMutationCounts() {
  // Send mutations to the auto analyze stateful service that aggregates mutations from all nodes
  // and triggers ANALYZE as necessary.

  auto mutation_counts = server_->GetPgNodeLevelMutationCounter()->GetAndClear();
  if (mutation_counts.size() == 0) {
    return Status::OK();
  }

  if (client_ == NULL) {
    client_ = std::make_unique<client::PgAutoAnalyzeServiceClient>();
    auto s = client_->Init(server_);
    if (!s.ok()) {
      client_ = nullptr;
      return s;
    }
  }

  stateful_service::IncreaseMutationCountersRequestPB req;
  stateful_service::IncreaseMutationCountersResponsePB resp;

  for (auto& [table_id, mutation_count] : mutation_counts) {
    req.add_table_mutation_counts();
    auto table_mutation_count = req.add_table_mutation_counts();
    table_mutation_count->set_table_id(table_id);
    table_mutation_count->set_mutation_count(mutation_count);
  }

  const Status s =
      client_->IncreaseMutationCounters(
          CoarseMonoClock::Now() + FLAGS_yb_client_admin_operation_timeout_sec * 1s, req, &resp);
  if (!s.ok()) {
    // It is possible that cluster-level mutation counters were updated but the response failed for
    // some other reason. In this case, unless we are certain that the cluster-level mutation
    // counters weren't updated, we avoid retrying to avoid double counting (i.e., we prefer
    // avoiding over-aggresive auto ANALYZE).
    if (s.IsTryAgain()) {
      for (auto& [table_id, mutation_count] : mutation_counts) {
        server_->GetPgNodeLevelMutationCounter()->Increase(table_id, mutation_count);
      }
    }
    return s;
  }

  // TODO: Handle per-table errors. Same as above, retry later if we are certain that the
  // cluster-level mutation counters weren't updated.

  return Status::OK();
}

void TableMutationCountSender::RunThread() {
  while (true) {
    auto deadline =
        CoarseMonoClock::now() + FLAGS_ysql_table_mutation_count_aggregate_interval_ms * 1ms;
    UNIQUE_LOCK(lock, mutex_);
    cond_.wait_until(GetLockForCondition(&lock), deadline);

    if (!should_run_) {
      VLOG(1) << "Table mutation count sender thread is finished";
      return;
    }

    WARN_NOT_OK(DoSendMutationCounts(), "Failed to send table mutation counts");
  }
}


} // namespace tserver
} // namespace yb
