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

#include <condition_variable>
#include <mutex>

#include "yb/rpc/strand.h"

#include "yb/common/transaction.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb {

class RWOperationCounter;

namespace tablet {

// Removes intents for specified transaction ids.
// Transaction should be previously aborted, if transaction was committed, then it is ignored.
class CleanupAbortsTask : public rpc::StrandTask {
 public:
  CleanupAbortsTask(TransactionIntentApplier* applier,
                    TransactionIdSet&& transactions_to_cleanup,
                    TransactionParticipantContext* participant_context,
                    TransactionStatusManager* status_manager,
                    RWOperationCounter* pending_op_counter,
                    const std::string& log_prefix);

  void Prepare(std::shared_ptr<CleanupAbortsTask> cleanup_task);
  void Run() override;
  void Done(const Status& status) override;

  virtual ~CleanupAbortsTask();

 private:
  const std::string& LogPrefix();
  void FilterTransactions();

  TransactionIntentApplier* applier_;
  TransactionIdSet transactions_to_cleanup_;
  TransactionParticipantContext& participant_context_;
  TransactionStatusManager& status_manager_;
  RWOperationCounter* const pending_op_counter_;
  const std::string& log_prefix_;
  size_t initial_number_of_transactions_ = 0;
  // The safe time to reach before removing intents, set once transactions are filtered.
  HybridTime wait_safe_time_;
  CoarseTimePoint wait_safe_time_deadline_;
  // Set by Run when the safe time was not reached, so Done enqueues the task again later.
  bool retry_ = false;
  std::shared_ptr<CleanupAbortsTask> retain_self_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::vector<TransactionId> erased_transactions_;
};

} // namespace tablet
} // namespace yb
