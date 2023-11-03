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

#include "yb/tablet/cleanup_intents_task.h"

#include "yb/tablet/transaction_intent_applier.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/transaction_participant_context.h"

#include "yb/util/status_log.h"

namespace yb {
namespace tablet {

CleanupIntentsTask::CleanupIntentsTask(
    TransactionParticipantContext* participant_context, TransactionIntentApplier* applier,
    RemoveReason reason, const TransactionId& id)
    : participant_context_(*participant_context), applier_(*applier), reason_(reason), id_(id) {}

void CleanupIntentsTask::Prepare(std::shared_ptr<CleanupIntentsTask> self) {
  retain_self_ = std::move(self);
}

void CleanupIntentsTask::Run() {
  RemoveIntentsData data;
  auto status = participant_context_.GetLastReplicatedData(&data);
  if (status.ok()) {
    status = applier_.RemoveIntents(data, reason_, id_);
  }
  WARN_NOT_OK(status,
              Format("Failed to remove intents of possible completed transaction $0", id_));
  VLOG(2) << "Cleaned intents for: " << id_;
}

void CleanupIntentsTask::Done(const Status& status) {
  retain_self_.reset();
}

} // namespace tablet
} // namespace yb
