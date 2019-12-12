//
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
//

#include "yb/tablet/transaction_participant.h"

#include <mutex>
#include <queue>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include <boost/optional/optional.hpp>

#include <boost/uuid/uuid_io.hpp>

#include "yb/rocksdb/write_batch.h"

#include "yb/client/transaction_rpc.h"

#include "yb/common/pgsql_error.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/thread_pool.h"

#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/tablet.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/thread_restrictions.h"

DECLARE_uint64(aborted_intent_cleanup_ms);

using namespace std::literals;
using namespace std::placeholders;

DEFINE_test_flag(uint64, transaction_delay_status_reply_usec_in_tests, 0,
                 "For tests only. Delay handling status reply by specified amount of usec.");
DEFINE_test_flag(double, transaction_ignore_applying_probability_in_tests, 0,
                 "Probability to ignore APPLYING update in tests.");
DEFINE_test_flag(bool, fail_in_apply_if_no_metadata, false,
                 "Fail when applying intents if metadata is not found.");
DEFINE_test_flag(int32, inject_load_transaction_delay_ms, 0,
                 "Inject delay before loading each transaction at startup.");

DEFINE_uint64(max_transactions_in_status_request, 128,
              "Request status for at most specified number of transactions at once.");

METRIC_DEFINE_simple_counter(
    tablet, transaction_load_attempts,
    "Total number of tries to load transaction metadata from the intents RocksDB",
    yb::MetricUnit::kTransactions);
METRIC_DEFINE_simple_counter(
    tablet, transaction_not_found,
    "Total number of missing transactions during load",
    yb::MetricUnit::kTransactions);
METRIC_DEFINE_simple_gauge_uint64(
    tablet, transactions_running,
    "Total number of transactions running in participant",
    yb::MetricUnit::kTransactions);

namespace yb {
namespace tablet {

namespace {

// Utility class to execute actions with specified delay.
class Delayer {
 public:
  void Delay(MonoTime when, std::function<void()> action) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!thread_.joinable()) {
        thread_ = std::thread([this] {
          Execute();
        });
      }
      queue_.emplace_back(when, std::move(action));
      cond_.notify_one();
    }
  }

  ~Delayer() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!thread_.joinable()) {
        return;
      }
      stop_ = true;
      cond_.notify_one();
    }
    thread_.join();
  }

 private:
  void Execute() {
    std::vector<std::function<void()>> actions;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
      if (!queue_.empty()) {
        auto now = MonoTime::Now();
        auto it = queue_.begin();
        while (it != queue_.end() && it->first <= now) {
          actions.push_back(std::move(it->second));
          ++it;
        }
        if (it != queue_.begin()) {
          queue_.erase(queue_.begin(), it);
          lock.unlock();
          auto se = ScopeExit([&lock, &actions] {
            actions.clear();
            lock.lock();
          });
          for (auto& action : actions) {
            action();
          }
        } else {
          cond_.wait_until(lock, queue_.front().first.ToSteadyTimePoint());
        }
      } else {
        cond_.wait(lock);
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable cond_;
  std::thread thread_;
  bool stop_ = false;
  std::deque<std::pair<MonoTime, std::function<void()>>> queue_;
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

  virtual bool RemoveUnlocked(const TransactionId& id, const std::string& reason) = 0;
  virtual void EnqueueRemoveUnlocked(const TransactionId& id) = 0;

  int64_t NextRequestIdUnlocked() {
    return ++request_serial_;
  }

  virtual const std::string& LogPrefix() const = 0;

  Delayer& delayer() {
    return delayer_;
  }

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

class RemoveIntentsTask : public rpc::ThreadPoolTask {
 public:
  RemoveIntentsTask(TransactionIntentApplier* applier,
                    TransactionParticipantContext* participant_context,
                    RunningTransactionContext* running_transaction_context,
                    const TransactionId& id)
      : applier_(*applier), participant_context_(*participant_context),
        running_transaction_context_(*running_transaction_context), id_(id)
  {}

  bool Prepare(RunningTransactionPtr transaction) {
    bool expected = false;
    if (!used_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return false;
    }

    transaction_ = std::move(transaction);
    return true;
  }

  void Run() override {
    RemoveIntentsData data;
    participant_context_.GetLastReplicatedData(&data);
    auto status = applier_.RemoveIntents(data, id_);
    LOG_IF_WITH_PREFIX(WARNING, !status.ok())
        << "Failed to remove intents of aborted transaction " << id_ << ": " << status;
    VLOG_WITH_PREFIX(2) << "Removed intents for: " << id_;
  }

  void Done(const Status& status) override {
    transaction_.reset();
  }

  virtual ~RemoveIntentsTask() {}

 private:
  const std::string& LogPrefix() const {
    return running_transaction_context_.LogPrefix();
  }

  TransactionIntentApplier& applier_;
  TransactionParticipantContext& participant_context_;
  RunningTransactionContext& running_transaction_context_;
  TransactionId id_;
  std::atomic<bool> used_{false};
  RunningTransactionPtr transaction_;
};

class CleanupAbortsTask : public rpc::ThreadPoolTask {
 public:
  CleanupAbortsTask(TransactionIntentApplier* applier,
                    TransactionIdSet&& transactions_to_cleanup,
                    TransactionParticipantContext* participant_context,
                    TransactionStatusManager* status_manager,
                    const std::string& log_prefix)
      : applier_(applier), transactions_to_cleanup_(std::move(transactions_to_cleanup)),
        participant_context_(*participant_context),
        status_manager_(*status_manager),
        log_prefix_(log_prefix) {}

  void Prepare(std::shared_ptr<CleanupAbortsTask> cleanup_task) {
    retain_self_ = std::move(cleanup_task);
  }

  void Run() override {
    size_t initial_number_of_transactions = transactions_to_cleanup_.size();

    FilterTransactions();

    if (transactions_to_cleanup_.empty()) {
      LOG_WITH_PREFIX(INFO)
          << "Nothing to cleanup of " << initial_number_of_transactions << " transactions";
      return;
    }

    // Update now to reflect time on the coordinator
    auto now = participant_context_.Now();

    // The calls to RequestStatusAt would have updated the local clock of the participant.
    // Wait for the propagated time to reach the current hybrid time.
    const MonoDelta kMaxTotalSleep = 10s;
    auto safetime = applier_->ApplierSafeTime(now, CoarseMonoClock::now() + kMaxTotalSleep);
    if (!safetime) {
      LOG_WITH_PREFIX(WARNING) << "Tablet application did not catch up in: " << kMaxTotalSleep;
      return;
    }

    for (const TransactionId transaction_id : transactions_to_cleanup_) {
      // If transaction is committed, no action required
      // TODO(dtxn) : Do batch processing of transactions,
      // because LocalCommitTime will acquire lock per each call.
      auto commit_time = status_manager_.LocalCommitTime(transaction_id);
      if (commit_time.is_valid()) {
        transactions_to_cleanup_.erase(transaction_id);
      }
    }

    RemoveIntentsData data;
    participant_context_.GetLastReplicatedData(&data);
    WARN_NOT_OK(applier_->RemoveIntents(data, transactions_to_cleanup_),
                "RemoveIntents for transaction cleanup in compaction failed.");
    LOG_WITH_PREFIX(INFO)
        << "Number of aborted transactions cleaned up: " << transactions_to_cleanup_.size()
        << " of " << initial_number_of_transactions;
  }

  void Done(const Status& status) override {
    transactions_to_cleanup_.clear();
    retain_self_ = nullptr;
  }

  virtual ~CleanupAbortsTask() {
    LOG_WITH_PREFIX(INFO) << "Cleanup Aborts Task finished.";
  }

 private:
  const std::string& LogPrefix() {
    return log_prefix_;
  }

  void FilterTransactions() {
    size_t left_wait = transactions_to_cleanup_.size();
    std::unique_lock<std::mutex> lock(mutex_);

    auto now = participant_context_.Now();
    auto tid = std::this_thread::get_id();

    for (const TransactionId& transaction_id : transactions_to_cleanup_) {
      VLOG_WITH_PREFIX(1) << "Checking if transaction needs to be cleaned up: " << transaction_id;

      // If transaction is committed, no action required
      auto commit_time = status_manager_.LocalCommitTime(transaction_id);
      if (commit_time.is_valid()) {
        auto warning_time = commit_time.AddMilliseconds(FLAGS_aborted_intent_cleanup_ms);
        if (now >= warning_time) {
          LOG_WITH_PREFIX(WARNING)
              << "Transaction Id: " << transaction_id << " committed too long ago.";
        }
        VLOG_WITH_PREFIX(2) << "Transaction committed, should not cleanup: " << transaction_id;
        erased_transactions_.push_back(transaction_id);
        --left_wait;
        continue;
      }

      static const std::string kRequestReason = "cleanup"s;
      // Get transaction status
      StatusRequest request = {
          &transaction_id,
          now,
          now,
          0, // serial no. Could use 0 here, because read_ht == global_limit_ht.
          // So we cannot accept status with time >= read_ht and < global_limit_ht.
          &kRequestReason,
          TransactionLoadFlags{},
          [transaction_id, this, &left_wait, tid](Result<TransactionStatusResult> result) {
            std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
            if (tid != std::this_thread::get_id()) {
              lock.lock();
            }
            // Best effort
            // Status of abort will result in cleanup of intents
            if ((result.ok() && result->status == TransactionStatus::ABORTED) ||
                (!result.ok() && result.status().IsNotFound())) {
              VLOG_WITH_PREFIX(3) << "Transaction being cleaned " << transaction_id << ".";
            } else {
              this->erased_transactions_.push_back(transaction_id);
              VLOG_WITH_PREFIX(2) << "Transaction not aborted, should not cleanup: "
                                  << transaction_id << ", " << result;
            }
            if (--left_wait == 0) {
              cond_.notify_one();
            }
          }
      };
      status_manager_.RequestStatusAt(request);
    }

    cond_.wait(lock, [&left_wait] { return left_wait == 0; });

    for (const auto& transaction_id : erased_transactions_) {
      transactions_to_cleanup_.erase(transaction_id);
    }
  }

  TransactionIntentApplier* applier_;
  TransactionIdSet transactions_to_cleanup_;
  TransactionParticipantContext& participant_context_;
  TransactionStatusManager& status_manager_;
  const std::string& log_prefix_;
  std::shared_ptr<CleanupAbortsTask> retain_self_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::vector<TransactionId> erased_transactions_;
};

class CleanupIntentsTask : public rpc::ThreadPoolTask {
 public:
  CleanupIntentsTask(
      TransactionParticipantContext* participant_context, TransactionIntentApplier* applier,
      const TransactionId& id)
      : participant_context_(*participant_context), applier_(*applier), id_(id) {}

  void Prepare(std::shared_ptr<CleanupIntentsTask> self) {
    retain_self_ = std::move(self);
  }

  void Run() override {
    RemoveIntentsData data;
    participant_context_.GetLastReplicatedData(&data);
    WARN_NOT_OK(applier_.RemoveIntents(data, id_),
                Format("Failed to remove intents of possible completed transaction $0", id_));
    VLOG(2) << "Cleaned intents for: " << id_;
  }

  void Done(const Status& status) override {
    retain_self_.reset();
  }

  virtual ~CleanupIntentsTask() {}

 private:
  TransactionParticipantContext& participant_context_;
  TransactionIntentApplier& applier_;
  TransactionId id_;
  std::shared_ptr<CleanupIntentsTask> retain_self_;
};

CHECKED_STATUS MakeAbortedStatus(const TransactionId& id) {
  return STATUS(TryAgain, Format("Transaction aborted: $0", id), Slice(),
      PgsqlError(YBPgErrorCode::YB_PG_IN_FAILED_SQL_TRANSACTION));
}

class RunningTransaction : public std::enable_shared_from_this<RunningTransaction> {
 public:
  RunningTransaction(TransactionMetadata metadata,
                     IntraTxnWriteId last_write_id,
                     RunningTransactionContext* context)
      : metadata_(std::move(metadata)),
        last_write_id_(last_write_id),
        context_(*context),
        remove_intents_task_(&context->applier_, &context->participant_context_, context,
                             metadata_.transaction_id),
        get_status_handle_(context->rpcs_.InvalidHandle()),
        abort_handle_(context->rpcs_.InvalidHandle()) {
  }

  ~RunningTransaction() {
    context_.rpcs_.Abort({&get_status_handle_, &abort_handle_});
  }

  const TransactionId& id() const {
    return metadata_.transaction_id;
  }

  const TransactionMetadata& metadata() const {
    return metadata_;
  }

  IntraTxnWriteId last_write_id() const {
    return last_write_id_;
  }

  void UpdateLastWriteId(IntraTxnWriteId value) {
    last_write_id_ = value;
  }

  HybridTime local_commit_time() const {
    return local_commit_time_;
  }

  void SetLocalCommitTime(HybridTime time) {
    local_commit_time_ = time;
  }

  void RequestStatusAt(client::YBClient* client,
                       const StatusRequest& request,
                       std::unique_lock<std::mutex>* lock) {
    DCHECK_LT(request.global_limit_ht, HybridTime::kMax);
    DCHECK_LE(request.read_ht, request.global_limit_ht);

    if (last_known_status_hybrid_time_ > HybridTime::kMin) {
      auto transaction_status =
          GetStatusAt(request.global_limit_ht, last_known_status_hybrid_time_, last_known_status_);
      // If we don't have status at global_limit_ht, then we should request updated status.
      if (transaction_status) {
        HybridTime last_known_status_hybrid_time = last_known_status_hybrid_time_;
        lock->unlock();
        request.callback(
            TransactionStatusResult{*transaction_status, last_known_status_hybrid_time});
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

    lock->unlock();
    SendStatusRequest(client, request_id, shared_self);
  }

  bool WasAborted() const {
    return last_known_status_ == TransactionStatus::ABORTED;
  }

  CHECKED_STATUS CheckAborted() const {
    if (WasAborted()) {
      return MakeAbortedStatus(id());
    }
    return Status::OK();
  }

  void Abort(client::YBClient* client,
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
    lock->unlock();
    VLOG_WITH_PREFIX(3) << "Abort request: " << was_empty;
    if (!was_empty) {
      return;
    }
    tserver::AbortTransactionRequestPB req;
    req.set_tablet_id(metadata_.status_tablet);
    req.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    req.set_propagated_hybrid_time(context_.participant_context_.Now().ToUint64());
    context_.rpcs_.RegisterAndStart(
        client::AbortTransaction(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client,
            &req,
            std::bind(&RunningTransaction::AbortReceived, this, _1, _2, shared_from_this())),
        &abort_handle_);
  }

  std::string ToString() const {
    return metadata_.ToString();
  }

  void ScheduleRemoveIntents(const RunningTransactionPtr& shared_self) {
    if (remove_intents_task_.Prepare(shared_self)) {
      context_.participant_context_.Enqueue(&remove_intents_task_);
      VLOG_WITH_PREFIX(1) << "Intents should be removed asynchronously";
    }
  }

 private:
  static boost::optional<TransactionStatus> GetStatusAt(
      HybridTime time,
      HybridTime last_known_status_hybrid_time,
      TransactionStatus last_known_status) {
    switch (last_known_status) {
      case TransactionStatus::ABORTED:
        return TransactionStatus::ABORTED;
      case TransactionStatus::COMMITTED:
        return last_known_status_hybrid_time > time
            ? TransactionStatus::PENDING
            : TransactionStatus::COMMITTED;
      case TransactionStatus::PENDING:
        if (last_known_status_hybrid_time >= time) {
          return TransactionStatus::PENDING;
        }
        return boost::none;
      default:
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, last_known_status);
    }
  }

  void SendStatusRequest(
      client::YBClient* client, int64_t serial_no, const RunningTransactionPtr& shared_self) {
    tserver::GetTransactionStatusRequestPB req;
    req.set_tablet_id(metadata_.status_tablet);
    req.add_transaction_id()->assign(
        pointer_cast<const char*>(metadata_.transaction_id.data), metadata_.transaction_id.size());
    req.set_propagated_hybrid_time(context_.participant_context_.Now().ToUint64());
    context_.rpcs_.RegisterAndStart(
        client::GetTransactionStatus(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client,
            &req,
            std::bind(&RunningTransaction::StatusReceived, this, client, _1, _2, serial_no,
                      shared_self)),
        &get_status_handle_);
  }

  void StatusReceived(client::YBClient* client,
                      const Status& status,
                      const tserver::GetTransactionStatusResponsePB& response,
                      int64_t serial_no,
                      const RunningTransactionPtr& shared_self) {
    auto delay_usec = FLAGS_transaction_delay_status_reply_usec_in_tests;
    if (delay_usec > 0) {
      context_.delayer().Delay(
          MonoTime::Now() + MonoDelta::FromMicroseconds(delay_usec),
          std::bind(&RunningTransaction::DoStatusReceived, this, client, status, response,
                    serial_no, shared_self));
    } else {
      DoStatusReceived(client, status, response, serial_no, shared_self);
    }
  }

  void DoStatusReceived(client::YBClient* client,
                        const Status& status,
                        const tserver::GetTransactionStatusResponsePB& response,
                        int64_t serial_no,
                        const RunningTransactionPtr& shared_self) {
    if (response.has_propagated_hybrid_time()) {
      context_.participant_context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }

    context_.rpcs_.Unregister(&get_status_handle_);
    decltype(status_waiters_) status_waiters;
    HybridTime time_of_status;
    TransactionStatus transaction_status;
    const bool ok = status.ok();
    int64_t new_request_id = -1;
    {
      std::unique_lock<std::mutex> lock(context_.mutex_);
      if (!ok) {
        status_waiters_.swap(status_waiters);
        lock.unlock();
        for (const auto& waiter : status_waiters) {
          waiter.callback(status);
        }
        return;
      }

      if (response.status_hybrid_time().size() == 1) {
        time_of_status = HybridTime(response.status_hybrid_time()[0]);
      } else {
        LOG(DFATAL) << "Wrong number of status hybrid time entries, exactly one entry expected: "
                    << response.ShortDebugString();
        time_of_status = HybridTime::kMin;
      }

      // Check for local_commit_time_ is not required for correctness, but useful for optimization.
      // So we could avoid unnecessary actions.
      if (local_commit_time_.is_valid()) {
        last_known_status_hybrid_time_ = local_commit_time_;
        last_known_status_ = TransactionStatus::COMMITTED;
      } else if (response.status().size() == 1) {
        if (last_known_status_hybrid_time_ <= time_of_status) {
          last_known_status_hybrid_time_ = time_of_status;
          last_known_status_ = response.status(0);
          if (response.status(0) == TransactionStatus::ABORTED) {
            context_.EnqueueRemoveUnlocked(id());
          }
        }
      } else {
        LOG(DFATAL) << "Wrong number of status entries, exactly one entry expected: "
                    << response.ShortDebugString();
      }

      time_of_status = last_known_status_hybrid_time_;
      transaction_status = last_known_status_;

      status_waiters = ExtractFinishedStatusWaitersUnlocked(
          serial_no, time_of_status, transaction_status);
      if (!status_waiters_.empty()) {
        new_request_id = context_.NextRequestIdUnlocked();
        VLOG_WITH_PREFIX(4) << "Waiters still present, send new status request: " << new_request_id;
      }
    }
    if (new_request_id >= 0) {
      SendStatusRequest(client, new_request_id, shared_self);
    }
    NotifyWaiters(serial_no, time_of_status, transaction_status, status_waiters);
  }

  // Extracts status waiters from status_waiters_ that could be notified at this point.
  // Extracted waiters also removed from status_waiters_.
  std::vector<StatusRequest> ExtractFinishedStatusWaitersUnlocked(
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

  // Notify provided status waiters.
  void NotifyWaiters(int64_t serial_no, HybridTime time_of_status,
                     TransactionStatus transaction_status,
                     const std::vector<StatusRequest>& status_waiters) {
    for (const auto& waiter : status_waiters) {
      auto status_for_waiter = GetStatusAt(
          waiter.global_limit_ht, time_of_status, transaction_status);
      if (status_for_waiter) {
        // We know status at global_limit_ht, so could notify waiter.
        waiter.callback(TransactionStatusResult{*status_for_waiter, time_of_status});
      } else if (time_of_status >= waiter.read_ht) {
        // It means that between read_ht and global_limit_ht transaction was pending.
        // It implies that transaction was not committed before request was sent.
        // We could safely respond PENDING to caller.
        LOG_IF_WITH_PREFIX(DFATAL, waiter.serial_no > serial_no)
            << "Notify waiter with request id greater than id of status request: "
            << waiter.serial_no << " vs " << serial_no;
        waiter.callback(TransactionStatusResult{TransactionStatus::PENDING, time_of_status});
      } else {
        waiter.callback(STATUS(TryAgain,
            Format("Cannot determine transaction status with read_ht $0, and global_limit_ht $1, "
                   "last known: $2 at $3", waiter.read_ht, waiter.global_limit_ht,
                   TransactionStatus_Name(transaction_status), time_of_status), Slice(),
            PgsqlError(YBPgErrorCode::YB_PG_IN_FAILED_SQL_TRANSACTION) ));
      }
    }
  }

  static Result<TransactionStatusResult> MakeAbortResult(
      const Status& status,
      const tserver::AbortTransactionResponsePB& response) {
    if (!status.ok()) {
      return status;
    }

    HybridTime status_time = response.has_status_hybrid_time()
         ? HybridTime(response.status_hybrid_time())
         : HybridTime::kInvalid;
    return TransactionStatusResult{response.status(), status_time};
  }

  void AbortReceived(const Status& status,
                     const tserver::AbortTransactionResponsePB& response,
                     const RunningTransactionPtr& shared_self) {
    if (response.has_propagated_hybrid_time()) {
      context_.participant_context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }

    decltype(abort_waiters_) abort_waiters;
    auto result = MakeAbortResult(status, response);

    VLOG_WITH_PREFIX(3) << "AbortReceived: " << yb::ToString(result);

    {
      std::lock_guard<std::mutex> lock(context_.mutex_);
      context_.rpcs_.Unregister(&abort_handle_);
      abort_waiters_.swap(abort_waiters);
      // kMax status_time means taht this status is not yet replicated and could be rejected.
      // So we could use it as reply to Abort, but cannot store it as transaction status.
      if (result.ok() && result->status_time != HybridTime::kMax) {
        last_known_status_ = result->status;
        last_known_status_hybrid_time_ = result->status_time;
      }
    }
    for (const auto& waiter : abort_waiters) {
      waiter(result);
    }
  }

  std::string LogPrefix() const {
    return Format(
        "$0 ID $1: ", context_.LogPrefix().substr(0, context_.LogPrefix().length() - 2), id());
  }

  TransactionMetadata metadata_;
  IntraTxnWriteId last_write_id_ = 0;
  RunningTransactionContext& context_;
  RemoveIntentsTask remove_intents_task_;
  HybridTime local_commit_time_ = HybridTime::kInvalid;

  TransactionStatus last_known_status_ = TransactionStatus::CREATED;
  HybridTime last_known_status_hybrid_time_ = HybridTime::kMin;
  std::vector<StatusRequest> status_waiters_;
  rpc::Rpcs::Handle get_status_handle_;
  rpc::Rpcs::Handle abort_handle_;
  std::vector<TransactionStatusCallback> abort_waiters_;
};

} // namespace

std::string TransactionApplyData::ToString() const {
  return Format(
      "{ transaction_id: $0 op_id: $1 commit_ht: $2 log_ht: $3 status_tablet: $4 }",
      transaction_id, op_id, commit_ht, log_ht, status_tablet);
}

class TransactionParticipant::Impl : public RunningTransactionContext {
 public:
  Impl(TransactionParticipantContext* context, TransactionIntentApplier* applier,
       const scoped_refptr<MetricEntity>& entity)
      : RunningTransactionContext(context, applier),
        log_prefix_(Format("T $0 P $1: ", context->tablet_id(), context->permanent_uuid())),
        check_status_handle_(rpcs_.InvalidHandle()) {
    LOG_WITH_PREFIX(INFO) << "Create";
    metric_transactions_running_ = METRIC_transactions_running.Instantiate(entity, 0);
    metric_transaction_load_attempts_ = METRIC_transaction_load_attempts.Instantiate(entity);
    metric_transaction_not_found_ = METRIC_transaction_not_found.Instantiate(entity);
    memset(&last_loaded_, 0, sizeof(last_loaded_));
  }

  ~Impl() {
    LOG_WITH_PREFIX(INFO) << "Stop";
    closing_.store(true, std::memory_order_release);
    transactions_.clear();
    TransactionsModifiedUnlocked();
    rpcs_.Shutdown();
    if (load_thread_.joinable()) {
      load_thread_.join();
    }
    while (checking_status_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(10ms);
    }
  }

  void Start() {
    LOG_WITH_PREFIX(INFO) << "Start";
    TryStartCheckLoadedTransactionsStatus(&all_loaded_, &started_);
  }

  // Adds new running transaction.
  bool Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch) {
    auto metadata = TransactionMetadata::FromPB(data);
    if (!metadata.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Invalid transaction id: " << metadata.status().ToString();
      return false;
    }
    WaitLoaded(metadata->transaction_id);
    bool store = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = transactions_.find(metadata->transaction_id);
      if (it == transactions_.end()) {
        if (WasTransactionRecentlyRemoved(metadata->transaction_id)) {
          return false;
        }
        VLOG_WITH_PREFIX(4) << "Create new transaction: " << metadata->transaction_id;
        transactions_.insert(std::make_shared<RunningTransaction>(*metadata, 0, this));
        TransactionsModifiedUnlocked();
        store = true;
      }
    }
    if (store) {
      docdb::KeyBytes key;
      AppendTransactionKeyPrefix(metadata->transaction_id, &key);
      auto data_copy = data;
      // We use hybrid time only for backward compatibility, actually wall time is required.
      data_copy.set_metadata_write_time(GetCurrentTimeMicros());
      auto value = data.SerializeAsString();
      write_batch->Put(key.data(), value);
    }
    return true;
  }

  HybridTime LocalCommitTime(const TransactionId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return HybridTime::kInvalid;
    }
    return (**it).local_commit_time();
  }

  std::pair<size_t, size_t> TEST_CountIntents() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ProcessRemoveQueueUnlocked();
    }

    std::pair<size_t, size_t> result(0, 0);
    auto iter = docdb::CreateRocksDBIterator(db_,
                                             key_bounds_,
                                             docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                             boost::none,
                                             rocksdb::kDefaultQueryId);
    for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
      ++result.first;
      // Count number of transaction, by counting metadata records.
      if (iter.key().size() == TransactionId::static_size() + 1) {
        ++result.second;
        auto key = iter.key();
        key.remove_prefix(1);
        auto id = CHECK_RESULT(FullyDecodeTransactionId(key));
        LOG_WITH_PREFIX(INFO) << "Stored txn meta: " << id;
      }
    }

    return result;
  }

  Result<TransactionMetadata> PrepareMetadata(const TransactionMetadataPB& pb) {
    if (pb.has_isolation()) {
      auto metadata = VERIFY_RESULT(TransactionMetadata::FromPB(pb));
      std::unique_lock<std::mutex> lock(mutex_);
      auto it = transactions_.find(metadata.transaction_id);
      if (it != transactions_.end()) {
        RETURN_NOT_OK((**it).CheckAborted());
      } else if (WasTransactionRecentlyRemoved(metadata.transaction_id)) {
        return MakeAbortedStatus(metadata.transaction_id);
      }
      return metadata;
    }

    auto id = VERIFY_RESULT(FullyDecodeTransactionId(pb.transaction_id()));

    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents or not.
    auto lock_and_iterator = LockAndFind(
        id, "metadata"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
    if (!lock_and_iterator.found()) {
      return STATUS(TryAgain,
                    Format("Unknown transaction, could be recently aborted: $0", id), Slice(),
                    PgsqlError(YBPgErrorCode::YB_PG_IN_FAILED_SQL_TRANSACTION));
    }
    RETURN_NOT_OK(lock_and_iterator.transaction().CheckAborted());
    return lock_and_iterator.transaction().metadata();
  }

  boost::optional<std::pair<TransactionMetadata, IntraTxnWriteId>> MetadataWithWriteId(
      const TransactionId& id) {
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents of not.
    auto lock_and_iterator = LockAndFind(
        id, "metadata with write id"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
    if (!lock_and_iterator.found()) {
      return boost::none;
    }
    auto& transaction = lock_and_iterator.transaction();
    return std::make_pair(transaction.metadata(), transaction.last_write_id());
  }

  void UpdateLastWriteId(const TransactionId& id, IntraTxnWriteId value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      LOG_IF_WITH_PREFIX(DFATAL, !WasTransactionRecentlyRemoved(id))
          << "Update last write id for unknown transaction: " << id;
      return;
    }
    (**it).UpdateLastWriteId(value);
  }

  void RequestStatusAt(const StatusRequest& request) {
    auto lock_and_iterator = LockAndFind(*request.id, *request.reason, request.flags);
    if (!lock_and_iterator.found()) {
      request.callback(
          STATUS_FORMAT(NotFound, "Request status of unknown transaction: $0", *request.id));
      return;
    }
    lock_and_iterator.transaction().RequestStatusAt(client(), request, &lock_and_iterator.lock);
  }

  // Registers a request, giving it a newly allocated id and returning this id.
  int64_t RegisterRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = NextRequestIdUnlocked();
    running_requests_.push_back(result);
    return result;
  }

  // Unregisters a previously registered request.
  void UnregisterRequest(int64_t request) {
    std::lock_guard<std::mutex> lock(mutex_);
    DCHECK(!running_requests_.empty());
    if (running_requests_.front() != request) {
      complete_requests_.push(request);
      return;
    }
    running_requests_.pop_front();
    while (!complete_requests_.empty() && complete_requests_.top() == running_requests_.front()) {
      complete_requests_.pop();
      running_requests_.pop_front();
    }

    CleanTransactionsUnlocked();
  }

  // Cleans transactions that are requested and now is safe to clean.
  // See RemoveUnlocked for details.
  void CleanTransactionsUnlocked() {
    ProcessRemoveQueueUnlocked();

    int64_t min_request = running_requests_.empty() ? std::numeric_limits<int64_t>::max()
                                                    : running_requests_.front();
    while (!cleanup_queue_.empty() && cleanup_queue_.front().request_id < min_request) {
      const auto& id = cleanup_queue_.front().transaction_id;
      auto it = transactions_.find(id);
      if (it != transactions_.end()) {
        (**it).ScheduleRemoveIntents(*it);
        RemoveTransaction(it);
      }
      VLOG_WITH_PREFIX(2) << "Cleaned from queue: " << id;
      cleanup_queue_.pop_front();
    }
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) {
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents of not.
    auto lock_and_iterator = LockAndFind(
        id, "abort"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
    if (!lock_and_iterator.found()) {
      callback(STATUS_FORMAT(NotFound, "Abort of unknown transaction: $0", id));
      return;
    }
    lock_and_iterator.transaction().Abort(client(), std::move(callback), &lock_and_iterator.lock);
  }

  CHECKED_STATUS CheckAborted(const TransactionId& id) {
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents of not.
    auto lock_and_iterator = LockAndFind(id, "check aborted"s, TransactionLoadFlags{});
    if (!lock_and_iterator.found()) {
      return MakeAbortedStatus(id);
    }
    return lock_and_iterator.transaction().CheckAborted();
  }

  void FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) {
    // TODO(dtxn) optimize locking
    for (auto& pair : *inout) {
      auto lock_and_iterator = LockAndFind(
          pair.first, "fill priorities"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
      if (!lock_and_iterator.found() || lock_and_iterator.transaction().WasAborted()) {
        pair.second = 0; // Minimal priority for already aborted transactions
      } else {
        pair.second = lock_and_iterator.transaction().metadata().priority;
      }
    }
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperationState> state, int64_t term) {
    if (state->request()->status() == TransactionStatus::APPLYING) {
      if (RandomActWithProbability(GetAtomicFlag(
          &FLAGS_transaction_ignore_applying_probability_in_tests))) {
        state->CompleteWithStatus(Status::OK());
        return;
      }
      participant_context_.SubmitUpdateTransaction(std::move(state), term);
      return;
    }

    if (state->request()->status() == TransactionStatus::CLEANUP) {
      auto id = FullyDecodeTransactionId(state->request()->transaction_id());
      if (!id.ok()) {
        state->CompleteWithStatus(id.status());
        return;
      }
      TransactionApplyData data = {
          term, *id, consensus::OpId(), HybridTime(), HybridTime(),
          std::string() };
      WARN_NOT_OK(ProcessCleanup(data, false /* force_remove */),
                  "Process cleanup failed");
      state->CompleteWithStatus(Status::OK());
      return;
    }

    auto status = STATUS_FORMAT(
        InvalidArgument, "Unexpected status in transaction participant Handle: $0", *state);
    LOG_WITH_PREFIX(DFATAL) << status;
    state->CompleteWithStatus(status);
  }

  CHECKED_STATUS ProcessReplicated(const ReplicatedData& data) {
    if (data.state.status() == TransactionStatus::APPLYING) {
      auto id = FullyDecodeTransactionId(data.state.transaction_id());
      if (!id.ok()) {
        return id.status();
      }
      // data.state.tablets contains only status tablet.
      if (data.state.tablets_size() != 1) {
        return STATUS_FORMAT(InvalidArgument,
                             "Expected only one table during APPLYING, state received: $0",
                             data.state);
      }
      HybridTime commit_time(data.state.commit_hybrid_time());
      TransactionApplyData apply_data = {
          data.leader_term, *id, data.op_id, commit_time, data.hybrid_time, data.state.tablets(0) };
      if (!data.already_applied) {
        return ProcessApply(apply_data);
      } else {
        return ProcessCleanup(apply_data, true /* force_remove */);
      }
    }

    auto status = STATUS_FORMAT(
        InvalidArgument, "Unexpected status in transaction participant ProcessReplicated: $0, $1",
        data.op_id, data.state);
    LOG_WITH_PREFIX(DFATAL) << status;
    return status;
  }

  void Cleanup(TransactionIdSet&& set, TransactionStatusManager* status_manager) {
    auto cleanup_aborts_task = std::make_shared<CleanupAbortsTask>(
        &applier_, std::move(set), &participant_context_, status_manager, LogPrefix());
    cleanup_aborts_task->Prepare(cleanup_aborts_task);
    participant_context_.Enqueue(cleanup_aborts_task.get());
  }

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data) {
    VLOG_WITH_PREFIX(2) << "Apply: " << data.ToString();

    {
      // It is our last chance to load transaction metadata, if missing.
      // Because it will be deleted when intents are applied.
      // We are not trying to cleanup intents here because we don't know whether this transaction
      // has intents of not.
      auto lock_and_iterator = LockAndFind(
          data.transaction_id, "pre apply"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
      if (!lock_and_iterator.found()) {
        // This situation is normal and could be caused by 2 scenarios:
        // 1) Write batch failed, but originator doesn't know that.
        // 2) Failed to notify status tablet that we applied transaction.
        LOG_WITH_PREFIX(WARNING) << Format("Apply of unknown transaction: $0", data);
        NotifyApplied(data);
        CHECK(!FLAGS_fail_in_apply_if_no_metadata);
        return Status::OK();
      }

      lock_and_iterator.transaction().SetLocalCommitTime(data.commit_ht);

      LOG_IF_WITH_PREFIX(DFATAL, data.log_ht < last_safe_time_)
          << "Apply transaction before last safe time " << data.transaction_id
          << ": " << data.log_ht << " vs " << last_safe_time_;
    }

    CHECK_OK(applier_.ApplyIntents(data));

    {
      // We are not trying to cleanup intents here because we don't know whether this transaction
      // has intents or not.
      auto lock_and_iterator = LockAndFind(
          data.transaction_id, "apply"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
      if (lock_and_iterator.found()) {
        RemoveUnlocked(lock_and_iterator.iterator, "applied"s);
      }
    }

    NotifyApplied(data);
    return Status::OK();
  }

  void NotifyApplied(const TransactionApplyData& data) {
    VLOG_WITH_PREFIX(4) << Format("NotifyApplied($0)", data);

    if (data.leader_term != OpId::kUnknownTerm) {
      tserver::UpdateTransactionRequestPB req;
      req.set_tablet_id(data.status_tablet);
      auto& state = *req.mutable_state();
      state.set_transaction_id(data.transaction_id.begin(), data.transaction_id.size());
      state.set_status(TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS);
      state.add_tablets(participant_context_.tablet_id());

      auto handle = rpcs_.Prepare();
      if (handle != rpcs_.InvalidHandle()) {
        *handle = UpdateTransaction(
            TransactionRpcDeadline(),
            nullptr /* remote_tablet */,
            client(),
            &req,
            [this, handle](const Status& status, HybridTime propagated_hybrid_time) {
              participant_context_.UpdateClock(propagated_hybrid_time);
              rpcs_.Unregister(handle);
              LOG_IF_WITH_PREFIX(WARNING, !status.ok()) << "Failed to send applied: " << status;
            });
        (**handle).SendRpc();
      }
    }
  }

  CHECKED_STATUS ProcessCleanup(const TransactionApplyData& data, bool force_remove) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = transactions_.find(data.transaction_id);
      if (it == transactions_.end()) {
        if (!force_remove) {
          return Status::OK();
        }
      } else {
        if (!RemoveUnlocked(it, "cleanup"s)) {
          VLOG_WITH_PREFIX(2) << "Have added aborted txn to cleanup queue : "
                              << data.transaction_id;
        }
      }
    }

    return Status::OK();
  }

  void SetDB(rocksdb::DB* db, const docdb::KeyBounds* key_bounds) {
    bool had_db = db_ != nullptr;
    db_ = db;
    key_bounds_ = key_bounds;

    // In case of truncate we should not reload transactions.
    if (!had_db) {
      std::unique_ptr <docdb::BoundedRocksDbIterator> iter(new docdb::BoundedRocksDbIterator(
          docdb::CreateRocksDBIterator(
              db_, &docdb::KeyBounds::kNoBounds,
              docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
              boost::none, rocksdb::kDefaultQueryId)));
      load_thread_ = std::thread(&Impl::LoadTransactions, this, iter.release());
    }
  }

  TransactionParticipantContext* participant_context() const {
    return &participant_context_;
  }

  size_t TEST_GetNumRunningTransactions() {
    std::lock_guard<std::mutex> lock(mutex_);
    VLOG_WITH_PREFIX(4) << "Transactions: " << yb::ToString(transactions_);
    return transactions_.size();
  }

 private:
  typedef boost::multi_index_container<RunningTransactionPtr,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun<
                  RunningTransaction, const TransactionId&, &RunningTransaction::id>
          >
      >
  > Transactions;

  void TransactionsModifiedUnlocked() {
    metric_transactions_running_->set_value(transactions_.size());
  }

  void EnqueueRemoveUnlocked(const TransactionId& id) override {
    auto now = participant_context_.Now();
    VLOG_WITH_PREFIX(4) << "EnqueueRemoveUnlocked: " << id << " at " << now;
    remove_queue_.emplace_back(RemoveQueueEntry{id, now});
    ProcessRemoveQueueUnlocked();
  }

  void ProcessRemoveQueueUnlocked() {
    if (!remove_queue_.empty()) {
      // When a transaction participant receives an "aborted" response from the coordinator,
      // it puts this transaction into a "remove queue", also storing the current hybrid
      // time. Then queue entries where time is less than current safe time are removed.
      //
      // This is correct because, from a transaction participant's point of view:
      //
      // (1) After we receive a response for a transaction status request, and
      // learn that the transaction is unknown to the coordinator, our local
      // hybrid time is at least as high as the local hybrid time on the
      // transaction status coordinator at the time the transaction was deleted
      // from the coordinator, due to hybrid time propagation on RPC response.
      //
      // (2) If our safe time is greater than the hybrid time when the
      // transaction was deleted from the coordinator, then we have already
      // applied this transaction's provisional records if the transaction was
      // committed.
      auto safe_time = participant_context_.SafeTimeForTransactionParticipant();
      if (!safe_time.is_valid()) {
        VLOG_WITH_PREFIX(3) << "Unable to obtain safe time to check remove queue";
        return;
      }
      VLOG_WITH_PREFIX(3) << "Checking remove queue: " << safe_time << ", "
                          << remove_queue_.front().time << ", " << remove_queue_.front().id;
      LOG_IF_WITH_PREFIX(DFATAL, safe_time < last_safe_time_)
          << "Safe time decreased: " << safe_time << " vs " << last_safe_time_;
      last_safe_time_ = safe_time;
      while (!remove_queue_.empty()) {
        auto it = transactions_.find(remove_queue_.front().id);
        if (it == transactions_.end() || (**it).local_commit_time().is_valid()) {
          // It is regular case, since the coordinator returns ABORTED for already applied
          // transaction. But this particular tablet could not yet apply it, so
          // it would add such transaction to remove queue.
          // And it is the main reason why we are waiting for safe time, before removing intents.
          VLOG_WITH_PREFIX(4) << "Evicting txn from remove queue, w/o removing intents: "
                              << remove_queue_.front().id;
          remove_queue_.pop_front();
          continue;
        }
        if (safe_time <= remove_queue_.front().time) {
          break;
        }
        VLOG_WITH_PREFIX(4) << "Removing from remove queue: " << remove_queue_.front().id;
        static const std::string kRemoveFromQueue = "remove_queue"s;
        RemoveUnlocked(remove_queue_.front().id, kRemoveFromQueue);
        remove_queue_.pop_front();
      }
    }
  }

  // Tries to remove transaction with specified id.
  // Returns true if transaction does not exist after call to this method, otherwise returns false,
  // which means that the transaction will be removed later.
  bool RemoveUnlocked(const TransactionId& id, const std::string& reason) override {
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return true;
    }
    return RemoveUnlocked(it, reason);
  }

  bool RemoveUnlocked(const Transactions::iterator& it, const std::string& reason) {
    if (running_requests_.empty()) {
      (**it).ScheduleRemoveIntents(*it);
      TransactionId txn_id = (**it).id();
      RemoveTransaction(it);
      VLOG_WITH_PREFIX(2) << "Cleaned transaction: " << txn_id << ", reason: " << reason
                          << ", left: " << transactions_.size();
      return true;
    }

    // We cannot remove the transaction at this point, because there are running requests
    // that are reading the provisional DB and could request status of this transaction.
    // So we store transaction in a queue and wait when all requests that we launched before our
    // attempt to remove this transaction are completed.
    // Since we try to remove the transaction after all its records are removed from the provisional
    // DB, it is safe to complete removal at this point, because it means that there will be no more
    // queries to status of this transactions.
    cleanup_queue_.push_back({request_serial_, (**it).id()});
    VLOG_WITH_PREFIX(2) << "Queued for cleanup: " << (**it).id() << ", reason: " << reason;
    return false;
  }

  struct LockAndFindResult {
    static Transactions::const_iterator UninitializedIterator() {
      static const Transactions empty_transactions;
      return empty_transactions.end();
    }

    std::unique_lock<std::mutex> lock;
    Transactions::const_iterator iterator = UninitializedIterator();
    bool recently_removed = false;

    bool found() const {
      return lock.owns_lock();
    }

    RunningTransaction& transaction() const {
      return **iterator;
    }
  };

  void WaitLoaded(const TransactionId& id) {
    if (all_loaded_.load(std::memory_order_acquire)) {
      return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    while (!all_loaded_.load(std::memory_order_acquire)) {
      if (last_loaded_ >= id) {
        break;
      }
      load_cond_.wait(lock);
    }
  }

  LockAndFindResult LockAndFind(
      const TransactionId& id, const std::string& reason, TransactionLoadFlags flags) {
    WaitLoaded(id);
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    if (it != transactions_.end()) {
      return LockAndFindResult{std::move(lock), it};
    }
    if (WasTransactionRecentlyRemoved(id)) {
      VLOG_WITH_PREFIX(1)
          << "Attempt to load recently removed transaction: " << id << ", for: " << reason;
      LockAndFindResult result;
      result.recently_removed = true;
      return result;
    }
    metric_transaction_not_found_->Increment();
    if (flags.Test(TransactionLoadFlag::kMustExist)) {
      LOG_WITH_PREFIX(WARNING) << "Transaction not found: " << id << ", for: " << reason;
    } else {
      LOG_WITH_PREFIX(INFO) << "Transaction not found: " << id << ", for: " << reason;
    }
    if (flags.Test(TransactionLoadFlag::kCleanup)) {
      VLOG_WITH_PREFIX(2) << "Schedule cleanup for: " << id;
      auto cleanup_task = std::make_shared<CleanupIntentsTask>(
          &participant_context_, &applier_, id);
      cleanup_task->Prepare(cleanup_task);
      participant_context_.Enqueue(cleanup_task.get());
    }
    return LockAndFindResult{};
  }

  void LoadTransactions(docdb::BoundedRocksDbIterator* iterator) {
    LOG_WITH_PREFIX(INFO) << "LoadTransactions";

    std::unique_ptr<docdb::BoundedRocksDbIterator> iterator_holder(iterator);
    docdb::KeyBytes key_bytes;
    TransactionId id;
    memset(&id, 0, sizeof(id));
    AppendTransactionKeyPrefix(id, &key_bytes);
    iterator->Seek(key_bytes.AsSlice());
    while (iterator->Valid()) {
      auto key = iterator->key();
      if (key[0] != docdb::ValueTypeAsChar::kTransactionId) {
        break;
      }
      key.remove_prefix(1);
      auto decode_id_result = DecodeTransactionId(&key);
      if (!decode_id_result.ok()) {
        LOG_WITH_PREFIX(DFATAL)
            << "Failed to decode transaction id from: " << key.ToDebugHexString();
        break;
      }
      id = *decode_id_result;
      key_bytes.Clear();
      AppendTransactionKeyPrefix(id, &key_bytes);
      if (key.empty()) { // Key fully consists of transaction id - it is metadata record.
        if (FLAGS_inject_load_transaction_delay_ms > 0) {
          std::this_thread::sleep_for(FLAGS_inject_load_transaction_delay_ms * 1ms);
        }
        LoadTransaction(iterator, id, iterator->value(), &key_bytes);
      }
      key_bytes.AppendValueType(docdb::ValueType::kMaxByte);
      iterator->Seek(key_bytes.AsSlice());
    }

    TryStartCheckLoadedTransactionsStatus(&started_, &all_loaded_);
    load_cond_.notify_all();
  }

  // iterator - rocks db iterator, that should be used for write id resolution.
  // id - transaction id to load.
  // value - transaction metadata record value.
  // key_bytes - buffer that contains key of current record, i.e. value type + transaction id.
  void LoadTransaction(
      docdb::BoundedRocksDbIterator* iterator, const TransactionId& id, const Slice& value,
      docdb::KeyBytes* key_bytes) {
    metric_transaction_load_attempts_->Increment();
    VLOG_WITH_PREFIX(1) << "Loading transaction: " << id;

    TransactionMetadataPB metadata_pb;

    if (!metadata_pb.ParseFromArray(value.cdata(), value.size())) {
      LOG_WITH_PREFIX(DFATAL) << "Unable to parse stored metadata: "
                              << value.ToDebugHexString();
      return;
    }

    auto metadata = TransactionMetadata::FromPB(metadata_pb);
    if (!metadata.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Loaded bad metadata: " << metadata.status();
      return;
    }

    key_bytes->AppendValueType(docdb::ValueType::kMaxByte);
    iterator->Seek(key_bytes->AsSlice());
    if (iterator->Valid()) {
      iterator->Prev();
    } else {
      iterator->SeekToLast();
    }
    key_bytes->Truncate(key_bytes->size() - 1);
    IntraTxnWriteId next_write_id = 0;
    while (iterator->Valid() && iterator->key().starts_with(*key_bytes)) {
      auto decoded_key = docdb::DecodeIntentKey(iterator->value());
      LOG_IF_WITH_PREFIX(DFATAL, !decoded_key.ok())
          << "Failed to decode intent while loading transaction " << id << ", "
          << iterator->key().ToDebugHexString() << " => "
          << iterator->value().ToDebugHexString() << ": " << decoded_key.status();
      if (decoded_key.ok() && docdb::HasStrong(decoded_key->intent_types)) {
        std::string rev_key = iterator->value().ToString();
        iterator->Seek(rev_key);
        // Delete could run in parallel to this load, since our deletes break snapshot read
        // we could get into situation when metadata and reverse record were successfully read,
        // but intent record could not be found.
        if (iterator->Valid() && iterator->key().starts_with(rev_key)) {
          VLOG_WITH_PREFIX(1)
              << "Found latest record for " << id
              << ": " << docdb::SubDocKey::DebugSliceToString(iterator->key())
              << " => " << iterator->value().ToDebugHexString();
          auto status = docdb::DecodeIntentValue(
              iterator->value(), Slice(id.data, id.size()), &next_write_id, nullptr /* body */);
          LOG_IF_WITH_PREFIX(DFATAL, !status.ok())
              << "Failed to decode intent value: " << status << ", "
              << docdb::SubDocKey::DebugSliceToString(iterator->key()) << " => "
              << iterator->value().ToDebugHexString();
          ++next_write_id;
        }
        break;
      }
      iterator->Prev();
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_loaded_ = metadata->transaction_id;
      if (FLAGS_max_transactions_in_status_request > 0) {
        check_status_queues_[metadata->status_tablet].push_back(metadata->transaction_id);
      }
      transactions_.insert(std::make_shared<RunningTransaction>(
          std::move(*metadata), next_write_id, this));
      TransactionsModifiedUnlocked();
    }
    load_cond_.notify_all();
  }

  client::YBClient* client() const {
    return participant_context_.client_future().get();
  }

  const std::string& LogPrefix() const override {
    return log_prefix_;
  }

  void RemoveTransaction(Transactions::iterator it) {
    auto now = CoarseMonoClock::now();
    CleanupRecentlyRemovedTransactions(now);
    auto& transaction = **it;
    recently_removed_transactions_cleanup_queue_.push_back({transaction.id(), now + 15s});
    LOG_IF_WITH_PREFIX(DFATAL, !recently_removed_transactions_.insert(transaction.id()).second)
        << "Transaction removed twice: " << transaction.id();
    VLOG_WITH_PREFIX(4) << "Remove transaction: " << transaction.id();
    transactions_.erase(it);
    TransactionsModifiedUnlocked();
  }

  void CleanupRecentlyRemovedTransactions(CoarseTimePoint now) {
    while (!recently_removed_transactions_cleanup_queue_.empty() &&
           recently_removed_transactions_cleanup_queue_.front().time <= now) {
      recently_removed_transactions_.erase(recently_removed_transactions_cleanup_queue_.front().id);
      recently_removed_transactions_cleanup_queue_.pop_front();
    }
  }

  bool WasTransactionRecentlyRemoved(const TransactionId& id) {
    CleanupRecentlyRemovedTransactions(CoarseMonoClock::now());
    return recently_removed_transactions_.count(id) != 0;
  }

  template <class F1, class F2>
  void TryStartCheckLoadedTransactionsStatus(const F1* flag_to_check, F2* flag_to_set) {
    bool check_loaded_transactions_status;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      *flag_to_set = true;
      check_loaded_transactions_status = *flag_to_check;
      if (check_loaded_transactions_status) {
        checking_status_.store(true, std::memory_order_release);
      }
    }
    if (check_loaded_transactions_status) {
      CheckLoadedTransactionsStatus();
    }
  }

  // Picks status tablet from checking_status_ map and request status of transactions
  // related to it.
  void CheckLoadedTransactionsStatus() {
    DCHECK(checking_status_.load(std::memory_order_acquire));
    auto max_transactions_in_status_request = FLAGS_max_transactions_in_status_request;
    if (closing_.load(std::memory_order_acquire) || check_status_queues_.empty() ||
        max_transactions_in_status_request <= 0) {
      checking_status_.store(false, std::memory_order_release);
      return;
    }

    // We access check_status_queues_ only during load and after that while resolving
    // transaction statuses, which is NOT concurrent.
    // So we could avoid doing synchronization here.
    auto& tablet_id_and_queue = *check_status_queues_.begin();
    tserver::GetTransactionStatusRequestPB req;
    req.set_tablet_id(tablet_id_and_queue.first);
    req.set_propagated_hybrid_time(participant_context_.Now().ToUint64());
    const auto& tablet_queue = tablet_id_and_queue.second;
    auto request_size = std::min<size_t>(max_transactions_in_status_request, tablet_queue.size());
    for (size_t i = 0; i != request_size; ++i) {
      const auto& txn_id = tablet_queue[i];
      VLOG_WITH_PREFIX(4) << "Checking txn status: " << txn_id;
      req.add_transaction_id()->assign(pointer_cast<const char*>(txn_id.begin()), txn_id.size());
    }
    rpcs_.RegisterAndStart(
        client::GetTransactionStatus(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client(),
            &req,
            std::bind(&Impl::StatusReceived, this, _1, _2, request_size)),
        &check_status_handle_);
  }

  void StatusReceived(const Status& status,
                      const tserver::GetTransactionStatusResponsePB& response,
                      size_t request_size) {
    VLOG_WITH_PREFIX(2) << "Received statuses: " << status << ", " << response.ShortDebugString();

    rpcs_.Unregister(&check_status_handle_);

    if (!status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Failed to request transaction statuses: " << status;
      if (status.IsAborted()) {
        check_status_queues_.clear();
      }
      CheckLoadedTransactionsStatus();
      return;
    }

    if (response.has_propagated_hybrid_time()) {
      participant_context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }

    if (response.status().size() != 1 && response.status().size() != request_size) {
      // Node with old software version would always return 1 status.
      LOG_WITH_PREFIX(DFATAL)
          << "Bad response size, expected " << request_size << " entries, but found: "
          << response.ShortDebugString();
      CheckLoadedTransactionsStatus();
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = check_status_queues_.begin();
      auto& queue = it->second;
      for (size_t i = 0; i != response.status().size(); ++i) {
        VLOG_WITH_PREFIX(4) << "Status of " << queue.front() << ": "
                            << TransactionStatus_Name(response.status(i));
        if (response.status(i) == TransactionStatus::ABORTED) {
          EnqueueRemoveUnlocked(queue.front());
        }
        queue.pop_front();
      }
      if (queue.empty()) {
        check_status_queues_.erase(it);
      }
    }

    CheckLoadedTransactionsStatus();
  }

  struct CleanupQueueEntry {
    int64_t request_id;
    TransactionId transaction_id;
  };

  std::string log_prefix_;

  rocksdb::DB* db_ = nullptr;
  const docdb::KeyBounds* key_bounds_;

  Transactions transactions_;
  // Ids of running requests, stored in increasing order.
  std::deque<int64_t> running_requests_;
  // Ids of complete requests, minimal request is on top.
  // Contains only ids greater than first running request id, otherwise entry is removed
  // from both collections.
  std::priority_queue<int64_t, std::vector<int64_t>, std::greater<void>> complete_requests_;

  // Queue of transaction ids that should be cleaned, paired with request that should be completed
  // in order to be able to do clean.
  // Guarded by RunningTransactionContext::mutex_
  std::deque<CleanupQueueEntry> cleanup_queue_;

  // Remove queue maintains transactions that could be cleaned when safe time for follower reaches
  // appropriate time for an entry.
  // Since we add entries with increasing time, this queue is ordered by time.
  struct RemoveQueueEntry {
    TransactionId id;
    HybridTime time;
  };

  // Guarded by RunningTransactionContext::mutex_
  std::deque<RemoveQueueEntry> remove_queue_;

  // Guarded by RunningTransactionContext::mutex_
  HybridTime last_safe_time_ = HybridTime::kMin;

  std::unordered_set<TransactionId, TransactionIdHash> recently_removed_transactions_;
  struct RecentlyRemovedTransaction {
    TransactionId id;
    CoarseTimePoint time;
  };
  std::deque<RecentlyRemovedTransaction> recently_removed_transactions_cleanup_queue_;

  std::unordered_map<TabletId, std::deque<TransactionId>> check_status_queues_;
  rpc::Rpcs::Handle check_status_handle_;

  scoped_refptr<AtomicGauge<uint64_t>> metric_transactions_running_;
  scoped_refptr<Counter> metric_transaction_load_attempts_;
  scoped_refptr<Counter> metric_transaction_not_found_;

  std::thread load_thread_;
  std::condition_variable load_cond_;
  TransactionId last_loaded_;
  std::atomic<bool> all_loaded_{false};
  std::atomic<bool> closing_{false};
  // True while status check for loaded transactions is in progress.
  std::atomic<bool> checking_status_{false};
  bool started_ = false;
};

TransactionParticipant::TransactionParticipant(
    TransactionParticipantContext* context, TransactionIntentApplier* applier,
    const scoped_refptr<MetricEntity>& entity)
    : impl_(new Impl(context, applier, entity)) {
}

TransactionParticipant::~TransactionParticipant() {
}

void TransactionParticipant::Start() {
  impl_->Start();
}

bool TransactionParticipant::Add(
    const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch) {
  return impl_->Add(data, write_batch);
}

Result<TransactionMetadata> TransactionParticipant::PrepareMetadata(
    const TransactionMetadataPB& pb) {
  return impl_->PrepareMetadata(pb);
}

boost::optional<std::pair<TransactionMetadata, IntraTxnWriteId>>
    TransactionParticipant::MetadataWithWriteId(
    const TransactionId& id) {
  return impl_->MetadataWithWriteId(id);
}

void TransactionParticipant::UpdateLastWriteId(const TransactionId& id, IntraTxnWriteId value) {
  return impl_->UpdateLastWriteId(id, value);
}

HybridTime TransactionParticipant::LocalCommitTime(const TransactionId& id) {
  return impl_->LocalCommitTime(id);
}

std::pair<size_t, size_t> TransactionParticipant::TEST_CountIntents() const {
  return impl_->TEST_CountIntents();
}

void TransactionParticipant::RequestStatusAt(const StatusRequest& request) {
  return impl_->RequestStatusAt(request);
}

int64_t TransactionParticipant::RegisterRequest() {
  return impl_->RegisterRequest();
}

void TransactionParticipant::UnregisterRequest(int64_t request) {
  impl_->UnregisterRequest(request);
}

void TransactionParticipant::Abort(const TransactionId& id,
                                   TransactionStatusCallback callback) {
  return impl_->Abort(id, std::move(callback));
}

void TransactionParticipant::Handle(
    std::unique_ptr<tablet::UpdateTxnOperationState> request, int64_t term) {
  impl_->Handle(std::move(request), term);
}

void TransactionParticipant::Cleanup(TransactionIdSet&& set) {
  return impl_->Cleanup(std::move(set), this);
}

Status TransactionParticipant::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

Status TransactionParticipant::CheckAborted(const TransactionId& id) {
  return impl_->CheckAborted(id);
}

void TransactionParticipant::FillPriorities(
    boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) {
  return impl_->FillPriorities(inout);
}

void TransactionParticipant::SetDB(rocksdb::DB* db, const docdb::KeyBounds* key_bounds) {
  impl_->SetDB(db, key_bounds);
}

TransactionParticipantContext* TransactionParticipant::context() const {
  return impl_->participant_context();
}

size_t TransactionParticipant::TEST_GetNumRunningTransactions() const {
  return impl_->TEST_GetNumRunningTransactions();
}

} // namespace tablet
} // namespace yb
