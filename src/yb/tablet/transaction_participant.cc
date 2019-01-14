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
#include <boost/scope_exit.hpp>

#include "yb/rocksdb/write_batch.h"

#include "yb/client/transaction_rpc.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/thread_pool.h"

#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/tablet.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"

DECLARE_uint64(aborted_intent_cleanup_ms);

using namespace std::literals;
using namespace std::placeholders;

DEFINE_uint64(transaction_delay_status_reply_usec_in_tests, 0,
              "For tests only. Delay handling status reply by specified amount of usec.");
DEFINE_double(transaction_ignore_applying_probability_in_tests, 0,
              "Probability to ignore APPLYING update in tests.");

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
          BOOST_SCOPE_EXIT(&lock, &actions) {
            actions.clear();
            lock.lock();
          } BOOST_SCOPE_EXIT_END;
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

  virtual bool RemoveUnlocked(const TransactionId& id) = 0;

  int64_t NextRequestIdUnlocked() {
    return ++request_serial_;
  }

  virtual const std::string& LogPrefix() const = 0;

 protected:
  friend class RunningTransaction;

  rpc::Rpcs rpcs_;
  TransactionParticipantContext& participant_context_;
  TransactionIntentApplier& applier_;
  int64_t request_serial_ = 0;
  std::mutex mutex_;
};

class RemoveIntentsTask : public rpc::ThreadPoolTask {
 public:
  RemoveIntentsTask(TransactionIntentApplier* applier, TransactionParticipantContext* context,
                    const TransactionId& id)
      : applier_(*applier), context_(*context), id_(id) {}

  bool Prepare(RunningTransactionPtr transaction) {
    bool expected = false;
    if (!used_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return false;
    }

    transaction_ = std::move(transaction);
    return true;
  }

  void Run() override {
    if (context_.IsLeader()) {
      auto status = applier_.RemoveIntents(id_);
      LOG_IF(WARNING, !status.ok()) << "Failed to remove intents of aborted transaction " << id_
                                    << ": " << status;
    }
  }

  void Done(const Status& status) override {
    transaction_.reset();
  }

  virtual ~RemoveIntentsTask() {}

 private:
  TransactionIntentApplier& applier_;
  TransactionParticipantContext& context_;
  TransactionId id_;
  std::atomic<bool> used_{false};
  RunningTransactionPtr transaction_;
};

class CleanupAbortsTask : public rpc::ThreadPoolTask {
 public:
  CleanupAbortsTask(TransactionIntentApplier* applier,
                     TransactionIdSet&& transactions_to_cleanup,
                     TransactionParticipantContext* participant_context,
                     TransactionStatusManager* status_manager)
      : applier_(applier), transactions_to_cleanup_(std::move(transactions_to_cleanup)),
        participant_context_(*participant_context),
        status_manager_(*status_manager) {}

  void Prepare(std::shared_ptr<CleanupAbortsTask> cleanup_task) {
    retain_self_ = std::move(cleanup_task);
  }

  void Cancel() {
    retain_self_ = nullptr;
  }

  void Run() override {
    size_t initial_number_of_transactions = transactions_to_cleanup_.size();

    FilterTransactions();

    if (transactions_to_cleanup_.empty()) {
      LOG(INFO) << "Nothing to cleanup of " << initial_number_of_transactions << " transactions";
      return;
    }

    // Update now to reflect time on the coordinator
    auto now = participant_context_.Now();

    // The calls to RequestStatusAt would have updated the local clock of the participant.
    // Wait for the propagated time to reach the current hybrid time.
    const MonoDelta kMaxTotalSleep = 10s;
    auto safetime = applier_->ApplierSafeTime(now, MonoTime::Now() + kMaxTotalSleep);
    if (!safetime) {
      LOG(WARNING) << "Tablet application did not catch up in: " << kMaxTotalSleep;
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

    WARN_NOT_OK(applier_->RemoveIntents(transactions_to_cleanup_),
                "RemoveIntents for transaction cleanup in compaction failed.");
    LOG(INFO) << "Number of aborted transactions cleaned up: " << transactions_to_cleanup_.size()
              << " of " << initial_number_of_transactions;
  }

  void Done(const Status& status) override {
    transactions_to_cleanup_.clear();
    retain_self_ = nullptr;
  }

  virtual ~CleanupAbortsTask() {
    LOG(INFO) << "Cleanup Aborts Task finished.";
  }

 private:
  void FilterTransactions() {
    size_t left_wait = transactions_to_cleanup_.size();
    std::unique_lock<std::mutex> lock(mutex_);

    auto now = participant_context_.Now();
    auto tid = std::this_thread::get_id();

    for (const TransactionId& transaction_id : transactions_to_cleanup_) {
      VLOG(1) << "Checking if transaction needs to be cleaned up: " << transaction_id;

      // If transaction is committed, no action required
      auto commit_time = status_manager_.LocalCommitTime(transaction_id);
      if (commit_time.is_valid()) {
        auto warning_time = commit_time.AddMilliseconds(FLAGS_aborted_intent_cleanup_ms);
        if (now >= warning_time) {
          LOG(WARNING) << "Transaction Id: " << transaction_id << " committed too long ago.";
        }
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
          MustExist::kFalse,
          [transaction_id, this, &left_wait, tid](Result<TransactionStatusResult> result) {
            std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
            if (tid != std::this_thread::get_id()) {
              lock.lock();
            }
            // Best effort
            // Status of abort will result in cleanup of intents
            if (result.ok() && (result->status == TransactionStatus::ABORTED)) {
              VLOG(3) << "Transaction being cleaned " << transaction_id << ".";
            } else {
              this->erased_transactions_.push_back(transaction_id);
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
  std::shared_ptr<CleanupAbortsTask> retain_self_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::vector<TransactionId> erased_transactions_;
};

class RunningTransaction : public std::enable_shared_from_this<RunningTransaction> {
 public:
  RunningTransaction(TransactionMetadata metadata,
                     IntraTxnWriteId last_write_id,
                     RunningTransactionContext* context)
      : metadata_(std::move(metadata)),
        last_write_id_(last_write_id),
        context_(*context),
        remove_intents_task_(&context->applier_, &context->participant_context_,
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
    lock->unlock();
    SendStatusRequest(client, request_id, shared_self);
  }

  void Abort(client::YBClient* client,
             TransactionStatusCallback callback,
             std::unique_lock<std::mutex>* lock) {
    bool was_empty = abort_waiters_.empty();
    abort_waiters_.push_back(std::move(callback));
    lock->unlock();
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
    req.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
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
      delayer_.Delay(
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

      DCHECK(response.has_status_hybrid_time() ||
             response.status() == TransactionStatus::ABORTED);
      time_of_status = response.has_status_hybrid_time()
          ? HybridTime(response.status_hybrid_time())
          : HybridTime::kMax;
      if (last_known_status_hybrid_time_ <= time_of_status) {
        last_known_status_hybrid_time_ = time_of_status;
        last_known_status_ = response.status();
        if (response.status() == TransactionStatus::ABORTED) {
          if (!local_commit_time_ && remove_intents_task_.Prepare(shared_self)) {
            context_.participant_context_.thread_pool().Enqueue(&remove_intents_task_);
            VLOG_WITH_PREFIX(1) << "Transaction should be aborted: " << id();
          }
          context_.RemoveUnlocked(id());
        }
      }
      time_of_status = last_known_status_hybrid_time_;
      transaction_status = last_known_status_;

      status_waiters = ExtractFinishedStatusWaitersUnlocked(
          serial_no, time_of_status, transaction_status);
      if (!status_waiters_.empty()) {
        new_request_id = context_.NextRequestIdUnlocked();
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
        LOG_IF(DFATAL, waiter.serial_no > serial_no)
            << "Notify waiter with request id greater than id of status request: "
            << waiter.serial_no << " vs " << serial_no;
        waiter.callback(TransactionStatusResult{TransactionStatus::PENDING, time_of_status});
      } else {
        waiter.callback(STATUS_FORMAT(
            TryAgain,
            "Cannot determine transaction status with read_ht $0, and global_limit_ht $1, "
                "last known: $2 at $3",
            waiter.read_ht,
            waiter.global_limit_ht,
            TransactionStatus_Name(transaction_status),
            time_of_status));
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
    {
      std::lock_guard<std::mutex> lock(context_.mutex_);
      context_.rpcs_.Unregister(&abort_handle_);
      abort_waiters_.swap(abort_waiters);
    }
    auto result = MakeAbortResult(status, response);
    for (const auto& waiter : abort_waiters) {
      waiter(result);
    }
  }

  const std::string& LogPrefix() const {
    return context_.LogPrefix();
  }

  TransactionMetadata metadata_;
  IntraTxnWriteId last_write_id_ = 0;
  RunningTransactionContext& context_;
  RemoveIntentsTask remove_intents_task_;
  HybridTime local_commit_time_ = HybridTime::kInvalid;

  TransactionStatus last_known_status_;
  HybridTime last_known_status_hybrid_time_ = HybridTime::kMin;
  std::vector<StatusRequest> status_waiters_;
  rpc::Rpcs::Handle get_status_handle_;
  rpc::Rpcs::Handle abort_handle_;
  std::vector<TransactionStatusCallback> abort_waiters_;

  // Used only in tests.
  Delayer delayer_;
};

} // namespace

std::string TransactionApplyData::ToString() const {
  return Format(
      "{ transaction_id: $0 op_id: $1 commit_ht: $2 log_ht: $3 status_tablet: $4 }",
      transaction_id, op_id, commit_ht, log_ht, status_tablet);
}

class TransactionParticipant::Impl : public RunningTransactionContext {
 public:
  explicit Impl(TransactionParticipantContext* context, TransactionIntentApplier* applier)
      : RunningTransactionContext(context, applier), log_prefix_(context->tablet_id() + ": ") {
    LOG_WITH_PREFIX(INFO) << "Start";
  }

  ~Impl() {
    transactions_.clear();
    rpcs_.Shutdown();
  }

  // Adds new running transaction.
  void Add(const TransactionMetadataPB& data, bool may_have_metadata,
           rocksdb::WriteBatch *write_batch) {
    auto metadata = TransactionMetadata::FromPB(data);
    if (!metadata.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Invalid transaction id: " << metadata.status().ToString();
      return;
    }
    bool store = false;
    if (may_have_metadata) {
      auto lock_and_iterator = LockAndFindOrLoad(
          metadata->transaction_id, "add"s, MustExist::kFalse);
      if (!lock_and_iterator.found()) {
        lock_and_iterator.lock = std::unique_lock<std::mutex>(mutex_);
        auto insert_result = transactions_.insert(
            std::make_shared<RunningTransaction>(*metadata, 0, this));
        lock_and_iterator.iterator = insert_result.first;
        store = insert_result.second;
      } else {
        DCHECK_EQ((**lock_and_iterator.iterator).metadata(), *metadata);
      }
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = transactions_.find(metadata->transaction_id);
      if (it == transactions_.end()) {
        transactions_.insert(std::make_shared<RunningTransaction>(*metadata, 0, this));
        store = true;
      } else {
        DCHECK_EQ((**it).metadata(), *metadata);
      }
    }
    if (store) {
      docdb::KeyBytes key;
      AppendTransactionKeyPrefix(metadata->transaction_id, &key);
      auto value = data.SerializeAsString();
      write_batch->Put(key.data(), value);
    }
  }

  HybridTime LocalCommitTime(const TransactionId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return HybridTime::kInvalid;
    }
    return (**it).local_commit_time();
  }

  size_t TEST_CountIntents() {
    size_t count = 0;
    auto iter = docdb::CreateRocksDBIterator(db_,
                                             docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                             boost::none,
                                             rocksdb::kDefaultQueryId);
    while (iter->Valid()) {
      count++;
      iter->Next();
    }

    return count;
  }

  boost::optional<TransactionMetadata> Metadata(const TransactionId& id) {
    auto lock_and_iterator = LockAndFindOrLoad(id, "metadata"s);
    if (!lock_and_iterator.found()) {
      return boost::none;
    }
    return lock_and_iterator.transaction().metadata();
  }

  boost::optional<std::pair<TransactionMetadata, IntraTxnWriteId>> MetadataWithWriteId(
      const TransactionId& id) {
    auto lock_and_iterator = LockAndFindOrLoad(id, "metadata with write id"s);
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
      LOG(DFATAL) << "Update last write id for unknown transaction: " << id;
      return;
    }
    (**it).UpdateLastWriteId(value);
  }

  void RequestStatusAt(const StatusRequest& request) {
    auto lock_and_iterator = LockAndFindOrLoad(*request.id, *request.reason, request.must_exist);
    if (!lock_and_iterator.found()) {
      request.callback(
          STATUS_FORMAT(NotFound, "Request status of unknown transaction: $0", *request.id));
      return;
    }
    lock_and_iterator.transaction().RequestStatusAt(client(), request, &lock_and_iterator.lock);
  }

  // Registers request, giving him newly allocated id and returning this id.
  int64_t RegisterRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = NextRequestIdUnlocked();
    running_requests_.push_back(result);
    return result;
  }

  // Unregisters previously registered request.
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
    int64_t min_request = running_requests_.empty() ? std::numeric_limits<int64_t>::max()
                                                    : running_requests_.front();
    while (!cleanup_queue_.empty() && cleanup_queue_.front().request_id < min_request) {
      const auto& id = cleanup_queue_.front().transaction_id;
      transactions_.erase(id);
      VLOG_WITH_PREFIX(2) << "Cleaned from queue: " << id;
      cleanup_queue_.pop_front();
    }
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) {
    auto lock_and_iterator = LockAndFindOrLoad(id, "abort"s);
    if (!lock_and_iterator.found()) {
      callback(STATUS_FORMAT(NotFound, "Abort of unknown transaction: $0", id));
      return;
    }
    lock_and_iterator.transaction().Abort(client(), std::move(callback), &lock_and_iterator.lock);
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
    LOG(DFATAL) << status;
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
    LOG(DFATAL) << status;
    return status;
  }

  void Cleanup(TransactionIdSet&& set, TransactionStatusManager* status_manager) {
    auto cleanup_aborts_task = std::make_shared<CleanupAbortsTask>(
        &applier_, std::move(set), &participant_context_, status_manager);
    cleanup_aborts_task->Prepare(cleanup_aborts_task);
    if (!participant_context_.thread_pool().Enqueue(cleanup_aborts_task.get())) {
      cleanup_aborts_task->Cancel();
    }
  }

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data) {
    {
      // It is our last chance to load transaction metadata, if missing.
      // Because it will be deleted when intents are applied.
      auto lock_and_iterator = LockAndFindOrLoad(data.transaction_id, "pre apply"s);
      if (!lock_and_iterator.found()) {
        // This situation is normal and could be caused by 2 scenarios:
        // 1) Write batch failed, but originator doesn't know that.
        // 2) Failed to notify status tablet that we applied transaction.
        LOG_WITH_PREFIX(WARNING) << Format("Apply of unknown transaction: $0", data);
        NotifyApplied(data);
        return Status::OK();
      }

      lock_and_iterator.transaction().SetLocalCommitTime(data.commit_ht);
    }

    CHECK_OK(applier_.ApplyIntents(data));

    {
      auto lock_and_iterator = LockAndFindOrLoad(data.transaction_id, "apply"s);
      if (lock_and_iterator.found()) {
        RemoveUnlocked(lock_and_iterator.iterator);
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
        if (!RemoveUnlocked(it)) {
          VLOG_WITH_PREFIX(2) << "Have added aborted txn to cleanup queue : "
                              << data.transaction_id;
        }
      }
    }

    auto status = applier_.RemoveIntents(data.transaction_id);
    LOG_IF_WITH_PREFIX(DFATAL, !status.ok()) << "Failed to remove intents for "
                                             << data.transaction_id << ": " << status;

    return Status::OK();
  }

  void SetDB(rocksdb::DB* db) {
    db_ = db;
  }

  TransactionParticipantContext* participant_context() const {
    return &participant_context_;
  }

  size_t TEST_GetNumRunningTransactions() {
    std::lock_guard<std::mutex> lock(mutex_);
    VLOG(4) << "Transactions: " << yb::ToString(transactions_);
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

  // Tries to remove transaction with specified id.
  // Returns true if transaction is not exists after call to this method, otherwise returns false.
  // Which means that transaction will be removed later.
  bool RemoveUnlocked(const TransactionId& id) override {
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return true;
    }
    return RemoveUnlocked(it);
  }

  bool RemoveUnlocked(const Transactions::iterator& it) {
    if (running_requests_.empty()) {
      TransactionId txn_id = (**it).id();
      transactions_.erase(it);
      VLOG_WITH_PREFIX(2) << "Cleaned transaction: " << txn_id
                          << ", left: " << transactions_.size();
      return true;
    }

    // We cannot remove transaction at this point, because there are running requests,
    // that reads provisional DB and could request status of this transaction.
    // So we store transaction in queue and wait when all requests that we launched before our try
    // to remove this transaction are completed.
    // Since we try to remove transaction after all its records is removed from provisional DB
    // it is safe to complete removal at this point, because it means that there will be no more
    // queries to status of this transactions.
    cleanup_queue_.push_back({request_serial_, (**it).id()});
    VLOG_WITH_PREFIX(2) << "Queued for cleanup: " << (**it).id();
    return false;
  }

  struct LockAndFindOrLoadResult {
    std::unique_lock<std::mutex> lock;
    Transactions::const_iterator iterator;

    bool found() const {
      return lock.owns_lock();
    }

    RunningTransaction& transaction() const {
      return **iterator;
    }
  };

  LockAndFindOrLoadResult LockAndFindOrLoad(const TransactionId& id, const std::string& reason,
                                            MustExist must_exist = MustExist::kTrue) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto it = transactions_.find(id);
      if (it != transactions_.end()) {
        return LockAndFindOrLoadResult{std::move(lock), it};
      }
    }

    LOG_WITH_PREFIX(INFO) << "Loading transaction: " << id << ", for: " << reason;

    docdb::KeyBytes key;
    AppendTransactionKeyPrefix(id, &key);
    auto iter = docdb::CreateRocksDBIterator(db_,
                                             docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                             boost::none,
                                             rocksdb::kDefaultQueryId);
    iter->Seek(key.AsSlice());
    if (!iter->Valid() || iter->key() != key.data()) {
      if (must_exist) {
        LOG_WITH_PREFIX(WARNING) << "Transaction not found: " << id << ", for: " << reason;
      } else {
        LOG_WITH_PREFIX(INFO) << "Transaction not found: " << id << ", for: " << reason;
      }
      return LockAndFindOrLoadResult{};
    }
    TransactionMetadataPB metadata_pb;

    if (!metadata_pb.ParseFromArray(iter->value().cdata(), iter->value().size())) {
      LOG_WITH_PREFIX(DFATAL) << "Unable to parse stored metadata: "
                              << iter->value().ToDebugHexString();
      return LockAndFindOrLoadResult{};
    }

    auto metadata = TransactionMetadata::FromPB(metadata_pb);
    if (!metadata.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Loaded bad metadata: " << metadata.status();
      return LockAndFindOrLoadResult{};
    }

    key.AppendValueType(docdb::ValueType::kMaxByte);
    iter->Seek(key.AsSlice());
    if (iter->Valid()) {
      iter->Prev();
    } else {
      iter->SeekToLast();
    }
    key.Truncate(key.size() - 1);
    IntraTxnWriteId next_write_id = 0;
    while (iter->Valid() && iter->key().starts_with(key)) {
      auto decoded_key = docdb::DecodeIntentKey(iter->value());
      LOG_IF_WITH_PREFIX(DFATAL, !decoded_key.ok())
          << "Failed to decode intent " << iter->value().ToDebugHexString() << ": "
          << decoded_key.status();
      if (decoded_key.ok() && docdb::HasStrong(decoded_key->intent_types)) {
        iter->Seek(iter->value());
        if (iter->Valid()) {
          VLOG_WITH_PREFIX(1)
              << "Found latest record: " << docdb::SubDocKey::DebugSliceToString(iter->key())
              << " => " << iter->value().ToDebugHexString();
          auto status = docdb::DecodeIntentValue(
              iter->value(), Slice(id.data, id.size()), &next_write_id, nullptr /* body */);
          LOG_IF_WITH_PREFIX(DFATAL, !status.ok()) << "Failed to decode intent value: " << status;
          ++next_write_id;
        }
        break;
      }
      iter->Prev();
    }

    std::unique_lock<std::mutex> lock(mutex_);
    auto it = transactions_.insert(std::make_shared<RunningTransaction>(
        std::move(*metadata), next_write_id, this)).first;

    return LockAndFindOrLoadResult{std::move(lock), it};
  }

  client::YBClient* client() const {
    return participant_context_.client_future().get().get();
  }

  const std::string& LogPrefix() const override {
    return log_prefix_;
  }

  struct CleanupQueueEntry {
    int64_t request_id;
    TransactionId transaction_id;
  };

  std::string log_prefix_;

  rocksdb::DB* db_ = nullptr;
  Transactions transactions_;
  // Ids of running requests, stored in increasing order.
  std::deque<int64_t> running_requests_;
  // Ids of complete requests, minimal request is on top.
  // Contains only ids greater than first running request id, otherwise entry is removed
  // from both collections.
  std::priority_queue<int64_t, std::vector<int64_t>, std::greater<void>> complete_requests_;
  // Queue of transaction ids that should be cleaned, paired with request that should be completed
  // in order to be able to do clean.
  std::deque<CleanupQueueEntry> cleanup_queue_;
};

TransactionParticipant::TransactionParticipant(
    TransactionParticipantContext* context, TransactionIntentApplier* applier)
    : impl_(new Impl(context, applier)) {
}

TransactionParticipant::~TransactionParticipant() {
}

void TransactionParticipant::Add(const TransactionMetadataPB& data, bool may_have_metadata,
                                 rocksdb::WriteBatch *write_batch) {
  impl_->Add(data, may_have_metadata, write_batch);
}

boost::optional<TransactionMetadata> TransactionParticipant::Metadata(const TransactionId& id) {
  return impl_->Metadata(id);
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

size_t TransactionParticipant::TEST_CountIntents() const {
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

CHECKED_STATUS TransactionParticipant::ProcessApply(const TransactionApplyData& data) {
  return impl_->ProcessApply(data);
}

Status TransactionParticipant::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

void TransactionParticipant::SetDB(rocksdb::DB* db) {
  impl_->SetDB(db);
}

TransactionParticipantContext* TransactionParticipant::context() const {
  return impl_->participant_context();
}

size_t TransactionParticipant::TEST_GetNumRunningTransactions() const {
  return impl_->TEST_GetNumRunningTransactions();
}

} // namespace tablet
} // namespace yb
