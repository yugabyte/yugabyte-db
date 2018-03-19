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

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/locks.h"
#include "yb/util/monotime.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_uint64(transaction_delay_status_reply_usec_in_tests, 0,
              "For tests only. Delay handling status reply by specified amount of usec.");

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

class RunningTransaction {
 public:
  RunningTransaction(TransactionMetadata metadata,
                     rpc::Rpcs* rpcs,
                     TransactionParticipantContext* context,
                     std::atomic<int64_t>* request_serial)
      : metadata_(std::move(metadata)),
        rpcs_(*rpcs),
        context_(*context),
        request_serial_(request_serial),
        get_status_handle_(rpcs->InvalidHandle()),
        abort_handle_(rpcs->InvalidHandle()) {
  }

  ~RunningTransaction() {
    rpcs_.Abort({&get_status_handle_, &abort_handle_});
  }

  const TransactionId& id() const {
    return metadata_.transaction_id;
  }

  const TransactionMetadata& metadata() const {
    return metadata_;
  }

  HybridTime local_commit_time() const {
    return local_commit_time_;
  }

  void SetLocalCommitTime(HybridTime time) {
    local_commit_time_ = time;
  }

  void RequestStatusAt(client::YBClient* client,
                       const StatusRequest& request,
                       std::unique_lock<std::mutex>* lock) const {
    if (last_known_status_hybrid_time_ > HybridTime::kMin) {
      auto transaction_status =
          GetStatusAt(request.global_limit_ht, last_known_status_hybrid_time_, last_known_status_);
      // If we don't have status at global_limit_ht, then we should request updated status.
      if (transaction_status) {
        lock->unlock();
        request.callback(
            TransactionStatusResult{*transaction_status, last_known_status_hybrid_time_});
        return;
      }
    }
    bool was_empty = status_waiters_.empty();
    status_waiters_.push_back(request);
    if (!was_empty) {
      return;
    }
    lock->unlock();
    SendStatusRequest(client, lock->mutex());
  }

  void Abort(client::YBClient* client,
             TransactionStatusCallback callback,
             std::unique_lock<std::mutex>* lock) const {
    bool was_empty = abort_waiters_.empty();
    abort_waiters_.push_back(std::move(callback));
    lock->unlock();
    if (!was_empty) {
      return;
    }
    tserver::AbortTransactionRequestPB req;
    req.set_tablet_id(metadata_.status_tablet);
    req.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    req.set_propagated_hybrid_time(context_.Now().ToUint64());
    rpcs_.RegisterAndStart(
        client::AbortTransaction(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client,
            &req,
            std::bind(&RunningTransaction::AbortReceived, this, _1, _2, lock->mutex())),
        &abort_handle_);
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

  void SendStatusRequest(client::YBClient* client, std::mutex* mutex) const {
    tserver::GetTransactionStatusRequestPB req;
    req.set_tablet_id(metadata_.status_tablet);
    req.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    req.set_propagated_hybrid_time(context_.Now().ToUint64());
    int64_t serial_no = ++*request_serial_;
    rpcs_.RegisterAndStart(
        client::GetTransactionStatus(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client,
            &req,
            std::bind(&RunningTransaction::StatusReceived, this, client, _1, _2, serial_no, mutex)),
        &get_status_handle_);
  }

  void StatusReceived(client::YBClient* client,
                      const Status& status,
                      const tserver::GetTransactionStatusResponsePB& response,
                      int64_t serial_no,
                      std::mutex* mutex) const {
    auto delay_usec = FLAGS_transaction_delay_status_reply_usec_in_tests;
    if (delay_usec > 0) {
      delayer_.Delay(
          MonoTime::Now() + MonoDelta::FromMicroseconds(delay_usec),
          std::bind(&RunningTransaction::DoStatusReceived, this, client, status, response,
                    serial_no, mutex));
    } else {
      DoStatusReceived(client, status, response, serial_no, mutex);
    }
  }

  void DoStatusReceived(client::YBClient* client,
                        const Status& status,
                        const tserver::GetTransactionStatusResponsePB& response,
                        int64_t serial_no,
                        std::mutex* mutex) const {
    if (response.has_propagated_hybrid_time()) {
      context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }

    rpcs_.Unregister(&get_status_handle_);
    decltype(status_waiters_) status_waiters;
    HybridTime time;
    TransactionStatus transaction_status;
    const bool ok = status.ok();
    bool send_new_request;
    {
      std::unique_lock<std::mutex> lock(*mutex);
      if (ok) {
        DCHECK(response.has_status_hybrid_time() ||
               response.status() == TransactionStatus::ABORTED);
        time = response.has_status_hybrid_time()
            ? HybridTime(response.status_hybrid_time())
            : HybridTime::kMax;
        if (last_known_status_hybrid_time_ <= time) {
          last_known_status_hybrid_time_ = time;
          last_known_status_ = response.status();
        }
        time = last_known_status_hybrid_time_;
        transaction_status = last_known_status_;

        status_waiters.reserve(status_waiters_.size());
        auto w = status_waiters_.begin();
        for (auto it = status_waiters_.begin(); it != status_waiters_.end(); ++it) {
          if (it->serial_no <= serial_no ||
              GetStatusAt(it->global_limit_ht, time, transaction_status) ||
              time < it->read_ht) {
            status_waiters.push_back(std::move(*it));
          } else {
            if (w != it) {
              *w = std::move(*it);
            }
            ++w;
          }
        }
        status_waiters_.erase(w, status_waiters_.end());
      } else {
        status_waiters_.swap(status_waiters);
      }
      send_new_request = !status_waiters_.empty();
    }
    if (send_new_request) {
      SendStatusRequest(client, mutex);
    }
    if (!ok) {
      for (const auto& waiter : status_waiters) {
        waiter.callback(status);
      }
      return;
    }
    for (const auto& waiter : status_waiters) {
      auto status_for_waiter = GetStatusAt(waiter.global_limit_ht, time, transaction_status);
      if (status_for_waiter) {
        // We know status at global_limit_ht, so could notify waiter.
        waiter.callback(TransactionStatusResult{*status_for_waiter, time});
      } else if (time >= waiter.read_ht) {
        // It means that between read_ht and global_limit_ht transaction was pending.
        // It implies that transaction was not committed before request was sent.
        // We could safely respond PENDING to caller.
        DCHECK_LE(waiter.serial_no, serial_no);
        waiter.callback(TransactionStatusResult{TransactionStatus::PENDING, time});
      } else {
        waiter.callback(STATUS_FORMAT(
            TryAgain,
            "Cannot determine transaction status with read_ht $0, and global_limit_ht $1, "
                "last known: $2 at $3",
            waiter.read_ht,
            waiter.global_limit_ht,
            TransactionStatus_Name(transaction_status),
            time));
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
                     std::mutex* mutex) const {
    if (response.has_propagated_hybrid_time()) {
      context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }

    decltype(abort_waiters_) abort_waiters;
    {
      std::lock_guard<std::mutex> lock(*mutex);
      rpcs_.Unregister(&abort_handle_);
      abort_waiters_.swap(abort_waiters);
    }
    auto result = MakeAbortResult(status, response);
    for (const auto& waiter : abort_waiters) {
      waiter(result);
    }
  }

  TransactionMetadata metadata_;
  rpc::Rpcs& rpcs_;
  TransactionParticipantContext& context_;
  std::atomic<int64_t>* request_serial_;
  HybridTime local_commit_time_ = HybridTime::kInvalid;

  mutable TransactionStatus last_known_status_;
  mutable HybridTime last_known_status_hybrid_time_ = HybridTime::kMin;
  mutable std::vector<StatusRequest> status_waiters_;
  mutable rpc::Rpcs::Handle get_status_handle_;
  mutable rpc::Rpcs::Handle abort_handle_;
  mutable std::vector<TransactionStatusCallback> abort_waiters_;

  // Used only in tests.
  mutable Delayer delayer_;
};

} // namespace

class TransactionParticipant::Impl {
 public:
  explicit Impl(TransactionParticipantContext* context)
      : context_(*context), log_prefix_(context->tablet_id() + ": ") {}

  ~Impl() {
    transactions_.clear();
    rpcs_.Shutdown();
  }

  // Adds new running transaction.
  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch) {
    auto metadata = TransactionMetadata::FromPB(data);
    if (!metadata.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Invalid transaction id: " << metadata.status().ToString();
      return;
    }
    bool store = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = transactions_.find(metadata->transaction_id);
      if (it == transactions_.end()) {
        transactions_.emplace(*metadata, &rpcs_, &context_, &request_serial_);
        store = true;
      } else {
        DCHECK_EQ(it->metadata(), *metadata);
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
    return it->local_commit_time();
  }

  boost::optional<TransactionMetadata> Metadata(const TransactionId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = FindOrLoad(id);
    if (it == transactions_.end()) {
      return boost::none;
    }
    return it->metadata();
  }

  void RequestStatusAt(const StatusRequest& request) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = FindOrLoad(*request.id);
    if (it == transactions_.end()) {
      lock.unlock();
      request.callback(
          STATUS_FORMAT(NotFound, "Request status of unknown transaction: $0", *request.id));
      return;
    }
    return it->RequestStatusAt(client(), request, &lock);
  }

  int64_t RegisterRequest() {
    return ++request_serial_;
  }

  void Abort(const TransactionId& id,
             TransactionStatusCallback callback) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = FindOrLoad(id);
    if (it == transactions_.end()) {
      lock.unlock();
      callback(STATUS_FORMAT(NotFound, "Abort of unknown transaction: $0", id));
      return;
    }
    return it->Abort(client(), std::move(callback), &lock);
  }

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // It is our last chance to load transaction metadata, if missing.
      // Because it will be deleted when intents are applied.
      FindOrLoad(data.transaction_id);
    }

    CHECK_OK(data.applier->ApplyIntents(data));

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = FindOrLoad(data.transaction_id);
      if (it == transactions_.end()) {
        // This situation is normal and could be caused by 2 scenarios:
        // 1) Write batch failed, but originator doesn't know that.
        // 2) Failed to notify status tablet that we applied transaction.
        LOG_WITH_PREFIX(WARNING) << "Apply of unknown transaction: " << data.transaction_id;
        return Status::OK();
      } else {
        transactions_.modify(it, [&data](RunningTransaction& transaction) {
          transaction.SetLocalCommitTime(data.commit_ht);
        });
        // TODO(dtxn) cleanup
      }
      if (data.mode == ProcessingMode::LEADER) {
        tserver::UpdateTransactionRequestPB req;
        req.set_tablet_id(data.status_tablet);
        auto& state = *req.mutable_state();
        state.set_transaction_id(data.transaction_id.begin(), data.transaction_id.size());
        state.set_status(TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS);
        state.add_tablets(context_.tablet_id());

        auto handle = rpcs_.Prepare();
        if (handle != rpcs_.InvalidHandle()) {
          *handle = UpdateTransaction(
              TransactionRpcDeadline(),
              nullptr /* remote_tablet */,
              client(),
              &req,
              [this, handle](const Status& status, HybridTime propagated_hybrid_time) {
                context_.UpdateClock(propagated_hybrid_time);
                rpcs_.Unregister(handle);
                LOG_IF_WITH_PREFIX(WARNING, !status.ok()) << "Failed to send applied: " << status;
              });
          (**handle).SendRpc();
        }
      }
    }
    return Status::OK();
  }

  void SetDB(rocksdb::DB* db) {
    db_ = db;
  }

  TransactionParticipantContext* context() const {
    return &context_;
  }

 private:
  typedef boost::multi_index_container<RunningTransaction,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun<RunningTransaction,
                                                const TransactionId&,
                                                &RunningTransaction::id>
          >
      >
  > Transactions;

  // TODO(dtxn) unlock during load
  Transactions::const_iterator FindOrLoad(const TransactionId& id) {
    auto it = transactions_.find(id);
    if (it != transactions_.end()) {
      return it;
    }

    docdb::KeyBytes key;
    AppendTransactionKeyPrefix(id, &key);
    auto iter = docdb::CreateRocksDBIterator(db_,
                                             docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                             boost::none,
                                             rocksdb::kDefaultQueryId);
    iter->Seek(key.data());
    if (!iter->Valid() || iter->key() != key.data()) {
      LOG_WITH_PREFIX(WARNING) << "Transaction not found: " << id;
      return it;
    }
    TransactionMetadataPB metadata_pb;

    if (!metadata_pb.ParseFromArray(iter->value().cdata(), iter->value().size())) {
      LOG_WITH_PREFIX(DFATAL) << "Unable to parse stored metadata: "
                              << iter->value().ToDebugHexString();
      return it;
    }

    auto metadata = TransactionMetadata::FromPB(metadata_pb);
    if (!metadata.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Loaded bad metadata: " << metadata.status();
      return it;
    }

    it = transactions_.emplace(std::move(*metadata), &rpcs_, &context_, &request_serial_).first;

    return it;
  }

  client::YBClient* client() const {
    return context_.client_future().get().get();
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  TransactionParticipantContext& context_;
  std::string log_prefix_;

  rocksdb::DB* db_ = nullptr;
  std::mutex mutex_;
  rpc::Rpcs rpcs_;
  Transactions transactions_;
  std::atomic<int64_t> request_serial_{0};
};

TransactionParticipant::TransactionParticipant(TransactionParticipantContext* context)
    : impl_(new Impl(context)) {
}

TransactionParticipant::~TransactionParticipant() {
}

void TransactionParticipant::Add(const TransactionMetadataPB& data,
                                 rocksdb::WriteBatch *write_batch) {
  impl_->Add(data, write_batch);
}

boost::optional<TransactionMetadata> TransactionParticipant::Metadata(const TransactionId& id) {
  return impl_->Metadata(id);
}

HybridTime TransactionParticipant::LocalCommitTime(const TransactionId& id) {
  return impl_->LocalCommitTime(id);
}

void TransactionParticipant::RequestStatusAt(const StatusRequest& request) {
  return impl_->RequestStatusAt(request);
}

int64_t TransactionParticipant::RegisterRequest() {
  return impl_->RegisterRequest();
}

void TransactionParticipant::Abort(const TransactionId& id,
                                   TransactionStatusCallback callback) {
  return impl_->Abort(id, std::move(callback));
}

CHECKED_STATUS TransactionParticipant::ProcessApply(const TransactionApplyData& data) {
  return impl_->ProcessApply(data);
}

void TransactionParticipant::SetDB(rocksdb::DB* db) {
  impl_->SetDB(db);
}

TransactionParticipantContext* TransactionParticipant::context() const {
  return impl_->context();
}

} // namespace tablet
} // namespace yb
