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

#include "yb/client/client.h"
#include "yb/docdb/wait_queue.h"
#include "yb/server/server_fwd.h"

namespace yb {
namespace docdb {

// This class is responsible for aggregating all wait-for relationships across WaitQueue instances
// on a tserver and reporting these relationships to each waiting transaction's respective
// transaction coordinator.
class LocalWaitingTxnRegistry : public WaitingTxnRegistry {
 public:
  explicit LocalWaitingTxnRegistry(
      const std::shared_future<client::YBClient*>& client_future, const server::ClockPtr& clock,
      const std::string& tserver_uuid, ThreadPool* thread_pool);

  ~LocalWaitingTxnRegistry();

  // Returns a "ScopedWaitingTxnRegistration" instance which registers a wait-for relationship with
  // this LocalWaitingTxnRegistry as long as this instance remains in scope. Once this
  // ScopedWaitingTxnRegistration is destructed, the LocalWaitingTxnRegistry no longer tracks the
  // registered wait-for relationship.
  std::unique_ptr<docdb::ScopedWaitingTxnRegistration> Create() override;

  Status RegisterWaitingFor(
      const TransactionId& waiting, std::shared_ptr<ConflictDataManager> blockers,
      const TabletId& status_tablet_id, docdb::ScopedWaitingTxnRegistration* wrapper);

  // Triggers a report of all wait-for relationships tracked by this instance to each waiting
  // transaction's coordinator.
  void SendWaitForGraph();

  void StartShutdown();

  void CompleteShutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace docdb
} // namespace yb
