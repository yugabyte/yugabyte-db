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

#include "yb/tablet/running_transaction.h"

#include <glog/logging.h>

#include "yb/client/transaction_rpc.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/tablet/transaction_participant_context.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/yb_pg_errcodes.h"

using namespace std::placeholders;
using namespace std::literals;

DEFINE_test_flag(uint64, transaction_delay_status_reply_usec_in_tests, 0,
                 "For tests only. Delay handling status reply by specified amount of usec.");

DEFINE_RUNTIME_int64(transaction_abort_check_interval_ms, 5000 * yb::kTimeMultiplier,
                     "Interval to check whether running transaction was aborted.");

DEFINE_RUNTIME_int64(transaction_abort_check_timeout_ms, 30000 * yb::kTimeMultiplier,
                     "Timeout used when checking for aborted transactions.");

DEFINE_test_flag(bool, pause_sending_txn_status_requests, false,
                 "When set, hold off sending transaction status requests until the flag is reset.");

namespace yb {
namespace tablet {

RunningTransaction::RunningTransaction(TransactionMetadata metadata,
                                       const TransactionalBatchData& last_batch_data,
                                       OneWayBitmap&& replicated_batches,
                                       HybridTime base_time_for_abort_check_ht_calculation,
                                       RunningTransactionContext* context)
    : metadata_(std::move(metadata)),
      last_batch_data_(last_batch_data),
      replicated_batches_(std::move(replicated_batches)),
      context_(*context),
      remove_intents_task_(&context->applier_, &context->participant_context_, context,
                           metadata_.transaction_id),
      get_status_handle_(context->rpcs_.InvalidHandle()),
      abort_handle_(context->rpcs_.InvalidHandle()),
      apply_intents_task_(&context->applier_, context, &apply_data_),
      abort_check_ht_(base_time_for_abort_check_ht_calculation.AddDelta(
                          1ms * FLAGS_transaction_abort_check_interval_ms)) {
}

RunningTransaction::~RunningTransaction() {
  LOG_IF(WARNING, !status_waiters_.empty()) << "RunningTransaction with active status_waiters_ "
                                            << "being destroyed. This could lead to stuck "
                                            << "WriteQuery object(s).";
  if (WasAborted()) {
    context_.NotifyAbortedTransactionDecrement(id());
  }
  context_.rpcs_.Abort({&get_status_handle_, &abort_handle_});
}

void RunningTransaction::AddReplicatedBatch(
  size_t batch_idx, boost::container::small_vector_base<uint8_t>* encoded_replicated_batches) {
  VLOG_WITH_PREFIX(4) << __func__ << "(" << batch_idx << ")";
  replicated_batches_.Set(batch_idx);
  encoded_replicated_batches->push_back(dockv::KeyEntryTypeAsChar::kBitSet);
  replicated_batches_.EncodeTo(encoded_replicated_batches);
}

void RunningTransaction::BatchReplicated(const TransactionalBatchData& value) {
  VLOG_WITH_PREFIX(4) << __func__ << "(" << value.ToString() << ")";
  last_batch_data_ = value;
}

void RunningTransaction::SetLocalCommitData(
    HybridTime time, const SubtxnSet& aborted_subtxn_set) {
  last_known_aborted_subtxn_set_ = aborted_subtxn_set;
  local_commit_time_ = time;
  last_known_status_hybrid_time_ = local_commit_time_;
  if (last_known_status_ == TransactionStatus::ABORTED) {
    context_.NotifyAbortedTransactionDecrement(id());
  }
  last_known_status_ = TransactionStatus::COMMITTED;
}

void RunningTransaction::Aborted() {
  VLOG_WITH_PREFIX(4) << __func__ << "()";

  if (last_known_status_ != TransactionStatus::ABORTED) {
    last_known_status_ = TransactionStatus::ABORTED;
    context_.NotifyAbortedTransactionIncrement(id());
  }
  last_known_status_hybrid_time_ = HybridTime::kMax;
}

void RunningTransaction::RequestStatusAt(const StatusRequest& request,
                                         std::unique_lock<std::mutex>* lock) {
  DCHECK_LE(request.global_limit_ht, HybridTime::kMax);
  DCHECK_LE(request.read_ht, request.global_limit_ht);

  if (request.status_tablet_id) {
    *request.status_tablet_id = status_tablet();
  }

  if (last_known_status_hybrid_time_ > HybridTime::kMin) {
    auto transaction_status =
        GetStatusAt(request.global_limit_ht, last_known_status_hybrid_time_, last_known_status_);
    // If we don't have status at global_limit_ht, then we should request updated status.
    if (transaction_status) {
      HybridTime last_known_status_hybrid_time = last_known_status_hybrid_time_;
      SubtxnSet local_commit_aborted_subtxn_set;
      if (transaction_status == TransactionStatus::COMMITTED ||
          transaction_status == TransactionStatus::PENDING) {
        local_commit_aborted_subtxn_set = last_known_aborted_subtxn_set_;
      }
      auto last_known_deadlock_status = last_known_deadlock_status_;
      lock->unlock();
      request.callback(TransactionStatusResult{
          *transaction_status, last_known_status_hybrid_time, local_commit_aborted_subtxn_set,
          last_known_deadlock_status});
      return;
    }
  }
  bool was_empty = status_waiters_.empty();
  status_waiters_.push_back(request);
  if (!was_empty) {
    return;
  }
  auto request_id = context_.NextRequestIdUnlocked();
  auto shared_self = shared_from_this();

  VLOG_WITH_PREFIX(4) << Format(
      "Existing status knowledge ($0, $1) does not satisfy requested: $2, sending: $3",
      TransactionStatus_Name(last_known_status_), last_known_status_hybrid_time_, request,
      request_id);
  auto status_tablet = shared_self->status_tablet();
  lock->unlock();
  SendStatusRequest(status_tablet, request_id, shared_self);
}

bool RunningTransaction::WasAborted() const {
  return last_known_status_ == TransactionStatus::ABORTED;
}

Status RunningTransaction::CheckAborted() const {
  if (WasAborted()) {
    return last_known_deadlock_status_.ok() ? MakeAbortedStatus(id()) : last_known_deadlock_status_;
  }
  return Status::OK();
}

void RunningTransaction::Abort(client::YBClient* client,
                               TransactionStatusCallback callback,
                               std::unique_lock<std::mutex>* lock) {
  if (last_known_status_ == TransactionStatus::ABORTED ||
      last_known_status_ == TransactionStatus::COMMITTED) {
    // Transaction is already in final state, so no reason to send abort request.
    VLOG_WITH_PREFIX(3) << "Abort shortcut: " << last_known_status_;
    TransactionStatusResult status{last_known_status_, last_known_status_hybrid_time_};
    lock->unlock();
    callback(status);
    return;
  }
  bool was_empty = abort_waiters_.empty();
  abort_waiters_.push_back(std::move(callback));
  auto status_tablet = this->status_tablet();
  abort_request_in_progress_ = true;
  lock->unlock();
  VLOG_WITH_PREFIX(3) << "Abort request: " << was_empty;
  if (!was_empty) {
    return;
  }
  tserver::AbortTransactionRequestPB req;
  req.set_tablet_id(status_tablet);
  req.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
  req.set_propagated_hybrid_time(context_.participant_context_.Now().ToUint64());
  context_.rpcs_.RegisterAndStart(
      client::AbortTransaction(
          TransactionRpcDeadline(),
          nullptr /* tablet */,
          client,
          &req,
          std::bind(&RunningTransaction::AbortReceived, this, status_tablet,
                    _1, _2, shared_from_this())),
      &abort_handle_);
}

std::string RunningTransaction::ToString() const {
  return Format(
      "{ metadata: $0 last_batch_data: $1 replicated_batches: $2 local_commit_time: $3 "
      "last_known_status: $4 last_known_status_hybrid_time: $5 status_waiters_size: $6 "
      "outstanding_status_requests: $7}",
      metadata_, last_batch_data_, replicated_batches_, local_commit_time_,
      TransactionStatus_Name(last_known_status_), last_known_status_hybrid_time_,
      status_waiters_.size(), outstanding_status_requests_.load(std::memory_order_relaxed));
}

void RunningTransaction::ScheduleRemoveIntents(
    const RunningTransactionPtr& shared_self, RemoveReason reason) {
  if (remove_intents_task_.Prepare(shared_self, reason)) {
    context_.participant_context_.StrandEnqueue(&remove_intents_task_);
    VLOG_WITH_PREFIX(1) << "Intents should be removed asynchronously";
  }
}

boost::optional<TransactionStatus> RunningTransaction::GetStatusAt(
    HybridTime time, HybridTime last_known_status_hybrid_time,
    TransactionStatus last_known_status) {
  switch (last_known_status) {
    case TransactionStatus::ABORTED: {
      return TransactionStatus::ABORTED;
    }
    case TransactionStatus::COMMITTED:
      return last_known_status_hybrid_time > time
          ? TransactionStatus::PENDING
          : TransactionStatus::COMMITTED;
    case TransactionStatus::PENDING:
      if (last_known_status_hybrid_time >= time) {
        return TransactionStatus::PENDING;
      }
      return boost::none;
    case TransactionStatus::CREATED: {
      // This can happen in case of transaction promotion. The first status request to the old
      // status tablet could have arrived and the transaction could have undergone promoted in
      // the interim. In that case, we just return the past known status (which could be CREATED
      // if this was the first ever txn status request).
      return boost::none;
    }
    default:
      FATAL_INVALID_ENUM_VALUE(TransactionStatus, last_known_status);
  }
}

void RunningTransaction::SendStatusRequest(
    const TabletId& status_tablet, int64_t serial_no, const RunningTransactionPtr& shared_self) {
  TRACE_FUNC();
  VTRACE(1, yb::ToString(metadata_.transaction_id));
  auto* client = context_.participant_context_.client_future().get();
  if (!client) {
    LOG(WARNING) << "Shutting down. Cannot get GetTransactionStatus: " << metadata_;
    return;
  }
  if (PREDICT_FALSE(FLAGS_TEST_pause_sending_txn_status_requests)) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "FLAGS_TEST_pause_sending_txn_status_requests set. Holding "
                                 << "off sending transaction status requests until flag is reset.";
    while (FLAGS_TEST_pause_sending_txn_status_requests) {
      SleepFor(10ms);
    }
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Resume sending transaction status requests "
                                 << "against status tablet: " << status_tablet;
  }
  outstanding_status_requests_.fetch_add(1, std::memory_order_relaxed);
  tserver::GetTransactionStatusRequestPB req;
  req.set_tablet_id(status_tablet);
  req.add_transaction_id()->assign(
      pointer_cast<const char*>(metadata_.transaction_id.data()), metadata_.transaction_id.size());
  req.set_propagated_hybrid_time(context_.participant_context_.Now().ToUint64());
  context_.rpcs_.RegisterAndStart(
      client::GetTransactionStatus(
          TransactionRpcDeadline(),
          nullptr /* tablet */,
          client,
          &req,
          std::bind(&RunningTransaction::StatusReceived, this, status_tablet, _1, _2, serial_no,
                    shared_self)),
      &get_status_handle_);
}

void RunningTransaction::StatusReceived(
    const TabletId& status_tablet,
    const Status& status,
    const tserver::GetTransactionStatusResponsePB& response,
    int64_t serial_no,
    const RunningTransactionPtr& shared_self) {
  auto delay_usec = FLAGS_TEST_transaction_delay_status_reply_usec_in_tests;
  if (delay_usec > 0) {
    context_.delayer().Delay(
        MonoTime::Now() + MonoDelta::FromMicroseconds(delay_usec),
        std::bind(&RunningTransaction::DoStatusReceived, this, status_tablet, status, response,
                  serial_no, shared_self));
  } else {
    DoStatusReceived(status_tablet, status, response, serial_no, shared_self);
  }
}

bool RunningTransaction::UpdateStatus(
    const TabletId& status_tablet, TransactionStatus transaction_status, HybridTime time_of_status,
    HybridTime coordinator_safe_time, SubtxnSet aborted_subtxn_set,
    const Status& expected_deadlock_status) {
  if (status_tablet != this->status_tablet()) {
    // Can happen in case of transaction promotion, should be okay to return an existing older
    // status as the subsequent requests would return the latest status.
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Passed in status tablet isn't the current active status "
                                 << "tablet of the txn. Not updating the txn status.";
    return last_known_status_ == TransactionStatus::ABORTED;
  }

  if (!local_commit_time_ && transaction_status != TransactionStatus::ABORTED) {
    // If we've already committed locally, then last_known_aborted_subtxn_set_ is already set
    // properly. Otherwise, we should update it here.
    last_known_aborted_subtxn_set_ = aborted_subtxn_set;
  }

  // Check for local_commit_time_ is not required for correctness, but useful for optimization.
  // So we could avoid unnecessary actions.
  if (local_commit_time_) {
    return false;
  }

  if (transaction_status == TransactionStatus::ABORTED && coordinator_safe_time) {
    time_of_status = coordinator_safe_time;
  }
  last_known_status_hybrid_time_ = time_of_status;

  // Reset last_known_deadlock_status_ with the latest status. deadlock status is currently
  // used in wait-queues alone and shouldn't cause any correctness issues.
  DCHECK(expected_deadlock_status.ok() || transaction_status == TransactionStatus::ABORTED);
  last_known_deadlock_status_ = expected_deadlock_status;

  if (transaction_status == last_known_status_) {
    return false;
  }
  if (last_known_status_ == TransactionStatus::ABORTED) {
    context_.NotifyAbortedTransactionDecrement(id());
  }
  last_known_status_ = transaction_status;

  return transaction_status == TransactionStatus::ABORTED;
}

void RunningTransaction::DoStatusReceived(const TabletId& status_tablet,
                                          const Status& status,
                                          const tserver::GetTransactionStatusResponsePB& response,
                                          int64_t serial_no,
                                          const RunningTransactionPtr& shared_self) {
  TRACE("$0: $1", __func__, response.ShortDebugString());
  VLOG_WITH_PREFIX(4) << __func__ << "(" << status << ", " << response.ShortDebugString() << ", "
                      << serial_no << ")" << " from status tablet: " << status_tablet;

  outstanding_status_requests_.fetch_sub(1, std::memory_order_relaxed);
  if (response.has_propagated_hybrid_time()) {
    context_.participant_context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
  }

  context_.rpcs_.Unregister(&get_status_handle_);
  decltype(status_waiters_) status_waiters;
  HybridTime time_of_status = HybridTime::kMin;
  TransactionStatus transaction_status = TransactionStatus::PENDING;
  Status expected_deadlock_status = Status::OK();
  SubtxnSet aborted_subtxn_set;
  const bool ok = status.ok();
  int64_t new_request_id = -1;
  TabletId current_status_tablet;
  {
    MinRunningNotifier min_running_notifier(&context_.applier_);
    std::unique_lock<std::mutex> lock(context_.mutex_);
    if (!ok) {
      status_waiters_.swap(status_waiters);
      lock.unlock();
      for (const auto& waiter : status_waiters) {
        waiter.callback(status);
      }
      return;
    }

    if (response.status_hybrid_time().size() != 1 || response.status().size() != 1 ||
        response.aborted_subtxn_set().size() > 1 || response.deadlock_reason().size() > 1) {
      LOG_WITH_PREFIX(DFATAL)
          << "Wrong number of status, status hybrid time, deadlock_reason, or aborted subtxn "
          << "set entries, exactly one entry expected: "
          << response.ShortDebugString();
    } else if (PREDICT_FALSE(response.aborted_subtxn_set().empty())) {
      YB_LOG_EVERY_N(WARNING, 1)
          << "Empty aborted_subtxn_set in transaction status response. "
          << "This should only happen when nodes are on different versions, e.g. during upgrade.";
    } else {
      auto aborted_subtxn_set_or_status = SubtxnSet::FromPB(
          response.aborted_subtxn_set(0).set());
      if (aborted_subtxn_set_or_status.ok()) {
        time_of_status = HybridTime(response.status_hybrid_time()[0]);
        transaction_status = response.status(0);
        aborted_subtxn_set = aborted_subtxn_set_or_status.get();
        if (!response.deadlock_reason().empty() &&
            response.deadlock_reason(0).code() != AppStatusPB::OK) {
          // response contains a deadlock specific error.
          expected_deadlock_status = StatusFromPB(response.deadlock_reason(0));
        }
      } else {
        LOG_WITH_PREFIX(DFATAL)
            << "Could not deserialize SubtxnSet: "
            << "error - " << aborted_subtxn_set_or_status.status().ToString()
            << " response - " << response.ShortDebugString();
      }
    }

    LOG_IF_WITH_PREFIX(DFATAL, response.coordinator_safe_time().size() > 1)
        << "Wrong number of coordinator safe time entries, at most one expected: "
        << response.ShortDebugString();
    auto coordinator_safe_time = response.coordinator_safe_time().size() == 1
        ? HybridTime::FromPB(response.coordinator_safe_time(0)) : HybridTime();
    auto did_abort_txn = UpdateStatus(
        status_tablet, transaction_status, time_of_status, coordinator_safe_time,
        aborted_subtxn_set, expected_deadlock_status);
    if (did_abort_txn) {
      context_.NotifyAbortedTransactionIncrement(id());
      context_.EnqueueRemoveUnlocked(
          id(), RemoveReason::kStatusReceived, &min_running_notifier, expected_deadlock_status);
    }

    time_of_status = last_known_status_hybrid_time_;
    transaction_status = last_known_status_;
    aborted_subtxn_set = last_known_aborted_subtxn_set_;

    status_waiters = ExtractFinishedStatusWaitersUnlocked(
        serial_no, time_of_status, transaction_status);
    if (!status_waiters_.empty()) {
      new_request_id = context_.NextRequestIdUnlocked();
      VLOG_WITH_PREFIX(4) << "Waiters still present, send new status request: " << new_request_id;
    }
    current_status_tablet = shared_self->status_tablet();
  }
  if (new_request_id >= 0) {
    SendStatusRequest(current_status_tablet, new_request_id, shared_self);
  }
  NotifyWaiters(
      serial_no, time_of_status, transaction_status, aborted_subtxn_set, status_waiters,
      expected_deadlock_status);
}

std::vector<StatusRequest> RunningTransaction::ExtractFinishedStatusWaitersUnlocked(
    int64_t serial_no, HybridTime time_of_status, TransactionStatus transaction_status) {
  if (transaction_status == TransactionStatus::ABORTED) {
    return std::move(status_waiters_);
  }
  std::vector<StatusRequest> result;
  result.reserve(status_waiters_.size());
  auto w = status_waiters_.begin();
  for (auto it = status_waiters_.begin(); it != status_waiters_.end(); ++it) {
    if (it->serial_no <= serial_no ||
        GetStatusAt(it->global_limit_ht, time_of_status, transaction_status) ||
        time_of_status < it->read_ht) {
      result.push_back(std::move(*it));
    } else {
      if (w != it) {
        *w = std::move(*it);
      }
      ++w;
    }
  }
  status_waiters_.erase(w, status_waiters_.end());
  return result;
}

void RunningTransaction::NotifyWaiters(int64_t serial_no, HybridTime time_of_status,
                                       TransactionStatus transaction_status,
                                       const SubtxnSet& aborted_subtxn_set,
                                       const std::vector<StatusRequest>& status_waiters,
                                       const Status& expected_deadlock_status) {
  for (const auto& waiter : status_waiters) {
    auto status_for_waiter =
        GetStatusAt(waiter.global_limit_ht, time_of_status, transaction_status);
    if (status_for_waiter) {
      // We know status at global_limit_ht, so could notify waiter.
      auto result = TransactionStatusResult{*status_for_waiter, time_of_status};
      if (result.status == TransactionStatus::COMMITTED ||
          result.status == TransactionStatus::PENDING) {
        result.aborted_subtxn_set = aborted_subtxn_set;
      }
      result.expected_deadlock_status = expected_deadlock_status;
      waiter.callback(std::move(result));
    } else if (time_of_status >= waiter.read_ht) {
      // It means that between read_ht and global_limit_ht transaction was pending.
      // It implies that transaction was not committed before request was sent.
      // We could safely respond PENDING to caller.
      LOG_IF_WITH_PREFIX(DFATAL, waiter.serial_no > serial_no)
          << "Notify waiter with request id greater than id of status request: "
          << waiter.serial_no << " vs " << serial_no;
      waiter.callback(TransactionStatusResult{
          TransactionStatus::PENDING, time_of_status, aborted_subtxn_set,
          expected_deadlock_status});
    } else {
      waiter.callback(STATUS(TryAgain,
          Format("Cannot determine transaction status with read_ht $0, and global_limit_ht $1, "
                 "last known: $2 at $3", waiter.read_ht, waiter.global_limit_ht,
                 TransactionStatus_Name(transaction_status), time_of_status), Slice(),
          PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) ));
    }
  }
}

Result<TransactionStatusResult> RunningTransaction::MakeAbortResult(
    const Status& status,
    const tserver::AbortTransactionResponsePB& response) {
  if (!status.ok()) {
    return status;
  }

  HybridTime status_time = response.has_status_hybrid_time()
       ? HybridTime(response.status_hybrid_time())
       : HybridTime::kInvalid;
  return TransactionStatusResult{response.status(), status_time, SubtxnSet()};
}

void RunningTransaction::AbortReceived(const TabletId& status_tablet,
                                       const Status& status,
                                       const tserver::AbortTransactionResponsePB& response,
                                       const RunningTransactionPtr& shared_self) {
  if (response.has_propagated_hybrid_time()) {
    context_.participant_context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
  }

  decltype(abort_waiters_) abort_waiters;
  auto result = MakeAbortResult(status, response);

  VLOG_WITH_PREFIX(3) << "AbortReceived: " << yb::ToString(result);

  {
    MinRunningNotifier min_running_notifier(&context_.applier_);
    std::lock_guard lock(context_.mutex_);
    LOG_IF(DFATAL, !abort_request_in_progress_)
        << "AbortReceived executed with abort_request_in_progress_ unset. Could lead to data "
        << "inconsistentcy issues in case of Geo-Partition workloads.";
    abort_request_in_progress_ = false;

    LOG_IF(DFATAL, status_tablet != shared_self->status_tablet())
        << "Status Tablet switched while Abort txn request was in progress. This might lead "
        << "to data consistency issues.";

    context_.rpcs_.Unregister(&abort_handle_);
    abort_waiters_.swap(abort_waiters);
    // kMax status_time means that this status is not yet replicated and could be rejected.
    // So we could use it as reply to Abort, but cannot store it as transaction status.
    if (result.ok() && result->status_time != HybridTime::kMax) {
      auto coordinator_safe_time = HybridTime::FromPB(response.coordinator_safe_time());
      if (UpdateStatus(
          status_tablet, result->status, result->status_time, coordinator_safe_time,
          result->aborted_subtxn_set, result->expected_deadlock_status)) {
        context_.NotifyAbortedTransactionIncrement(id());
        context_.EnqueueRemoveUnlocked(
            id(), RemoveReason::kAbortReceived, &min_running_notifier,
            result->expected_deadlock_status);
      }
    }
  }
  for (const auto& waiter : abort_waiters) {
    waiter(result);
  }
}

std::string RunningTransaction::LogPrefix() const {
  return Format(
      "$0 ID $1: ", context_.LogPrefix().substr(0, context_.LogPrefix().length() - 2), id());
}

Status MakeAbortedStatus(const TransactionId& id) {
  return STATUS(
      TryAgain, Format("Transaction aborted: $0", id), Slice(),
      PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE));
}

void RunningTransaction::SetApplyData(const docdb::ApplyTransactionState& apply_state,
                                      const TransactionApplyData* data,
                                      ScopedRWOperation* operation) {
  // TODO(savepoints): Add test to ensure that apply_state.aborted is properly set here.
  apply_state_ = apply_state;
  bool active = apply_state_.active();
  if (active) {
    // We are trying to set processing_apply before starting the actual process of applying, and
    // unset it after we complete processing.
    processing_apply_.store(true, std::memory_order_release);
  }

  if (data) {
    if (!active) {
      LOG_WITH_PREFIX(DFATAL)
          << "Starting processing apply, but provided data in inactive state: " << data->ToString();
      return;
    }

    apply_data_ = *data;
    apply_data_.apply_state = &apply_state_;

    LOG_IF_WITH_PREFIX(DFATAL, local_commit_time_ != data->commit_ht)
        << "Commit time does not match: " << local_commit_time_ << " vs " << data->commit_ht;

    if (apply_intents_task_.Prepare(shared_from_this(), operation)) {
      context_.participant_context_.StrandEnqueue(&apply_intents_task_);
    } else {
      LOG_WITH_PREFIX(DFATAL) << "Unable to prepare apply intents task";
    }
  }

  if (!active) {
    processing_apply_.store(false, std::memory_order_release);

    VLOG_WITH_PREFIX(3) << "Finished applying intents";

    MinRunningNotifier min_running_notifier(&context_.applier_);
    std::lock_guard lock(context_.mutex_);
    context_.RemoveUnlocked(
        id(), RemoveReason::kLargeApplied, &min_running_notifier);
  }
}

void RunningTransaction::SetApplyOpId(const OpId& op_id) {
  apply_record_op_id_ = op_id;
}

bool RunningTransaction::ProcessingApply() const {
  return processing_apply_.load(std::memory_order_acquire);
}

const TabletId& RunningTransaction::status_tablet() const {
  return metadata_.status_tablet;
}

void RunningTransaction::UpdateTransactionStatusLocation(const TabletId& new_status_tablet) {
  metadata_.old_status_tablet = std::move(metadata_.status_tablet);
  metadata_.status_tablet = new_status_tablet;
}

void RunningTransaction::UpdateAbortCheckHT(HybridTime now, UpdateAbortCheckHTMode mode) {
  if (last_known_status_ == TransactionStatus::ABORTED ||
      last_known_status_ == TransactionStatus::COMMITTED) {
    abort_check_ht_ = HybridTime::kMax;
    return;
  }
  // When we send a status request, we schedule the transaction status to be re-checked around the
  // same time the request is supposed to time out. When we get a status response (normal case, no
  // timeout), we go back to the normal interval of re-checking the status of this transaction.
  auto delta_ms = mode == UpdateAbortCheckHTMode::kStatusRequestSent
      ? FLAGS_transaction_abort_check_timeout_ms
      : FLAGS_transaction_abort_check_interval_ms;
  abort_check_ht_ = now.AddDelta(1ms * delta_ms);
}

} // namespace tablet
} // namespace yb
