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

#include "yb/tablet/apply_intents_task.h"

#include "yb/docdb/docdb.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/tablet/running_transaction.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/status_log.h"

using namespace std::literals;

DEFINE_UNKNOWN_int64(apply_intents_task_injected_delay_ms, 0,
             "Inject such delay before applying intents for large transactions. "
             "Could be used to throttle the apply speed.");

DEFINE_test_flag(int32, pause_and_skip_apply_intents_task_loop_ms, 0,
                 "If set to a value greater than zero, each loop of the apply intents task will "
                 "sleep for the specified duration and continue without doing apply work.");

namespace yb {
namespace tablet {

ApplyIntentsTask::ApplyIntentsTask(TransactionIntentApplier* applier,
                                   RunningTransactionContext* running_transaction_context,
                                   const TransactionApplyData* apply_data)
    : applier_(*applier), running_transaction_context_(*running_transaction_context),
      apply_data_(*apply_data) {}

bool ApplyIntentsTask::Prepare(RunningTransactionPtr transaction, ScopedRWOperation* operation) {
  bool expected = false;
  if (!used_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return false;
  }

  transaction_ = std::move(transaction);
  operation_ = std::move(*DCHECK_NOTNULL(operation));
  return true;
}

void ApplyIntentsTask::Run() {
  VLOG_WITH_PREFIX(4) << __func__;

  for (;;) {
    AtomicFlagSleepMs(&FLAGS_apply_intents_task_injected_delay_ms);

    if (running_transaction_context_.Closing()) {
      VLOG_WITH_PREFIX(1) << "Abort because of shutdown";
      break;
    }

    const auto pause_and_skip_ms = ANNOTATE_UNPROTECTED_READ(
        FLAGS_TEST_pause_and_skip_apply_intents_task_loop_ms);
    if (PREDICT_FALSE(pause_and_skip_ms > 0)) {
      SleepFor(pause_and_skip_ms * 1ms);
      continue;
    }

    auto result = applier_.ApplyIntents(apply_data_);
    if (!result.ok()) {
      LOG_WITH_PREFIX(DFATAL)
          << "Failed to apply intents " << apply_data_.ToString() << ": " << result.status();
      break;
    }

    transaction_->SetApplyData(*result);
    VLOG_WITH_PREFIX(2) << "Performed next apply step: " << result->ToString();

    if (!result->active()) {
      break;
    }
  }
}

void ApplyIntentsTask::Done(const Status& status) {
  WARN_NOT_OK(status, "Apply intents task failed");
  operation_.Reset();
  transaction_.reset();
}

std::string ApplyIntentsTask::LogPrefix() const {
  return transaction_ ? transaction_->LogPrefix() : running_transaction_context_.LogPrefix();
}

} // namespace tablet
} // namespace yb
