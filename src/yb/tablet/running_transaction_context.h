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

#include <stdint.h>

#include <functional>
#include <mutex>
#include <type_traits>

#include "yb/util/flags.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/integral_types.h"

#include "yb/rpc/rpc.h"

#include "yb/tablet/transaction_intent_applier.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/delayer.h"
#include "yb/util/math_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_callback.h"

namespace yb {
namespace tablet {

class MinRunningNotifier {
 public:
  explicit MinRunningNotifier(TransactionIntentApplier* applier) : applier_(applier) {}

  void Satisfied() {
    satisfied_ = true;
  }

  ~MinRunningNotifier() {
    if (satisfied_ && applier_) {
      applier_->MinRunningHybridTimeSatisfied();
    }
  }
 private:
  bool satisfied_ = false;
  TransactionIntentApplier* applier_;
};

class RunningTransaction;

typedef std::shared_ptr<RunningTransaction> RunningTransactionPtr;

class RunningTransactionContext {
 public:
  RunningTransactionContext(TransactionParticipantContext* participant_context,
                            TransactionIntentApplier* applier)
      : participant_context_(*participant_context), applier_(*applier) {
  }

  virtual ~RunningTransactionContext() {}

  virtual bool RemoveUnlocked(
      const TransactionId& id, RemoveReason reason, MinRunningNotifier* min_running_notifier) = 0;

  virtual void EnqueueRemoveUnlocked(
      const TransactionId& id, RemoveReason reason, MinRunningNotifier* min_running_notifier,
      const Status& expected_deadlock_status) = 0;

  virtual void NotifyAbortedTransactionIncrement(const TransactionId& id) = 0;

  virtual void NotifyAbortedTransactionDecrement(const TransactionId& id) = 0;

  int64_t NextRequestIdUnlocked() {
    return ++request_serial_;
  }

  virtual const std::string& LogPrefix() const = 0;

  Delayer& delayer() {
    return delayer_;
  }

  virtual bool Closing() const = 0;

 protected:
  friend class RunningTransaction;

  rpc::Rpcs rpcs_;
  TransactionParticipantContext& participant_context_;
  TransactionIntentApplier& applier_;
  int64_t request_serial_ = 0;
  std::mutex mutex_;

  // Used only in tests.
  Delayer delayer_;
};

} // namespace tablet
} // namespace yb
