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

#include <atomic>
#include <string>

#include "yb/tablet/running_transaction_context.h"
#include "yb/common/transaction.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/util/strand.h"

namespace yb {
namespace tablet {
class TransactionIntentApplier;
class TransactionParticipantContext;
enum class RemoveReason;

// Used by RunningTransaction to remove its intents.
class RemoveIntentsTask : public rpc::StrandTask {
 public:
  RemoveIntentsTask(TransactionIntentApplier* applier,
                    TransactionParticipantContext* participant_context,
                    RunningTransactionContext* running_transaction_context,
                    const TransactionId& id);

  bool Prepare(RunningTransactionPtr transaction, RemoveReason reason);
  void Run() override;
  void Done(const Status& status) override;

  virtual ~RemoveIntentsTask() = default;

 private:
  std::string LogPrefix() const;

  TransactionIntentApplier& applier_;
  TransactionParticipantContext& participant_context_;
  RunningTransactionContext& running_transaction_context_;
  RemoveReason reason_;
  TransactionId id_;
  std::atomic<bool> used_{false};
  RunningTransactionPtr transaction_;
};

} // namespace tablet
} // namespace yb
