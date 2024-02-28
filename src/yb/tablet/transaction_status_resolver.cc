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

#include "yb/tablet/transaction_status_resolver.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/transaction_rpc.h"

#include "yb/common/wire_protocol.h"

#include "yb/rpc/rpc.h"

#include "yb/tablet/transaction_participant_context.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"

DEFINE_test_flag(int32, inject_status_resolver_delay_ms, 0,
                 "Inject delay before launching transaction status resolver RPC.");

DEFINE_test_flag(int32, inject_status_resolver_unregister_rpc_delay_ms, 0,
                 "Inject delay before unregistering rpc call after receiving the response.");

DEFINE_test_flag(int32, inject_status_resolver_complete_delay_ms, 0,
                 "Inject delay before counting down latch in transaction status resolver "
                 "complete.");

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace tablet {

class TransactionStatusResolver::Impl {
 public:
  Impl(TransactionParticipantContext* participant_context, rpc::Rpcs* rpcs,
       int max_transactions_per_request, TransactionStatusResolverCallback callback)
      : participant_context_(*participant_context), rpcs_(*rpcs),
        max_transactions_per_request_(max_transactions_per_request), callback_(std::move(callback)),
        log_prefix_(participant_context->LogPrefix()), handle_(rpcs_.InvalidHandle()) {}

  ~Impl() {
    LOG_IF_WITH_PREFIX(DFATAL, !closing_.load(std::memory_order_acquire))
        << "Destroy resolver without Shutdown";
  }

  void Shutdown() {
    closing_.store(true, std::memory_order_release);
    for (;;) {
      if (run_latch_.WaitFor(10s)) {
        break;
      }
      LOG_WITH_PREFIX(DFATAL) << "Long wait for transaction status resolver to shutdown";
    }
  }

  void Start(CoarseTimePoint deadline) {
    VLOG_WITH_PREFIX(2) << "Start, queues: " << queues_.size();

    deadline_ = deadline;
    run_latch_.Reset(1);
    Execute();
  }

  std::future<Status> ResultFuture() {
    return result_promise_.get_future();
  }

  bool Running() {
    return run_latch_.count() != 0;
  }

  void Add(const TabletId& status_tablet, const TransactionId& transaction_id) {
    LOG_IF(DFATAL, run_latch_.count()) << "Add while running";
    queues_[status_tablet].push_back(transaction_id);
  }

 private:
  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  void Execute() {
    LOG_IF(DFATAL, !run_latch_.count()) << "Execute while running is false";

    if (CoarseMonoClock::now() >= deadline_) {
      Complete(STATUS(TimedOut, "Timed out to resolve transaction statuses"));
      return;
    }
    if (closing_.load(std::memory_order_acquire)) {
      Complete(STATUS(Aborted, "Aborted because of shutdown"));
      return;
    }
    if (queues_.empty() || max_transactions_per_request_ <= 0) {
      Complete(Status::OK());
      return;
    }

    // We access queues_ only while adding transaction and after that while resolving
    // transaction statuses, which is NOT concurrent.
    // So we could avoid doing synchronization here.
    auto& tablet_id_and_queue = *queues_.begin();
    auto client_result = participant_context_.client();
    if (!client_result.ok()) {
      Complete(client_result.status());
      return;
    }
    auto client = client_result.get();
    if (!client) {
      Complete(STATUS(Aborted, "Aborted because cannot start RPC"));
    }
    client->LookupTabletById(
        tablet_id_and_queue.first,
        nullptr /* table */,
        master::IncludeInactive::kFalse,
        master::IncludeDeleted::kTrue,
        std::min(deadline_, TransactionRpcDeadline()),
        std::bind(&Impl::LookupTabletDone, this, _1),
        client::UseCache::kTrue);
  }

  void LookupTabletDone(const Result<client::internal::RemoteTabletPtr>& result) {
    auto& tablet_id_and_queue = *queues_.begin();
    const auto& tablet_id = tablet_id_and_queue.first;
    const auto& tablet_queue = tablet_id_and_queue.second;
    auto request_size = std::min<size_t>(max_transactions_per_request_, tablet_queue.size());

    if (!result.ok()) {
      const auto& status = result.status();
      LOG_WITH_PREFIX(WARNING) << "Failed to request transaction statuses: " << status;
      if (status.IsAborted()) {
        Complete(status);
      } else {
        Execute();
      }
      return;
    }

    auto tablet = *result;
    if (!tablet) {
      HandleTabletDeleted(request_size);
      return;
    }

    tserver::GetTransactionStatusRequestPB req;
    req.set_tablet_id(tablet_id);
    req.set_propagated_hybrid_time(participant_context_.Now().ToUint64());

    for (size_t i = 0; i != request_size; ++i) {
      const auto& txn_id = tablet_queue[i];
      VLOG_WITH_PREFIX(4) << "Checking txn status: " << txn_id;
      req.add_transaction_id()->assign(pointer_cast<const char*>(txn_id.data()), txn_id.size());
    }

    AtomicFlagSleepMs(&FLAGS_TEST_inject_status_resolver_delay_ms);

    auto client_result = participant_context_.client();
    if (!client_result.ok()) {
      Complete(client_result.status());
      return;
    }
    auto client = client_result.get();
    if (!client || !rpcs_.RegisterAndStart(
        client::GetTransactionStatus(
            std::min(deadline_, TransactionRpcDeadline()),
            nullptr /* tablet */,
            client,
            &req,
            std::bind(&Impl::StatusReceived, this, _1, _2, request_size)),
        &handle_)) {
      Complete(STATUS(Aborted, "Aborted because cannot start RPC"));
    }
  }

  void HandleTabletDeleted(size_t request_size) {
    VLOG_WITH_PREFIX(2) << "Transaction tablet is deleted";

    auto it = queues_.begin();
    auto& queue = it->second;
    // If the transaction status tablet has been deleted, all unapplied intents referring to it
    // are assumed to be aborted transactions.
    status_infos_.clear();
    status_infos_.resize(request_size);
    for (size_t i = 0; i != request_size; ++i) {
      auto& status_info = status_infos_[i];
      status_info.status_tablet = it->first;
      status_info.transaction_id = queue.front();
      status_info.status = TransactionStatus::ABORTED;
      status_info.status_ht = HybridTime::kMax;
      status_info.coordinator_safe_time = HybridTime();
      VLOG_WITH_PREFIX(4) << "Status: " << status_info.ToString();
      queue.pop_front();
    }

    if (queue.empty()) {
      VLOG_WITH_PREFIX(2) << "Processed queue for: " << it->first;
      queues_.erase(it);
    }

    callback_(status_infos_);

    Execute();
  }

  void StatusReceived(Status status,
                      const tserver::GetTransactionStatusResponsePB& response,
                      int request_size) {
    VLOG_WITH_PREFIX(2) << "Received statuses: " << status << ", " << response.ShortDebugString();
    DEBUG_ONLY_TEST_SYNC_POINT("TransactionStatusResolver::Impl::StatusReceived");
    AtomicFlagSleepMs(&FLAGS_TEST_inject_status_resolver_unregister_rpc_delay_ms);
    rpcs_.Unregister(&handle_);

    if (status.ok() && response.has_error()) {
      status = StatusFromPB(response.error().status());
    }

    if (!status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Failed to request transaction statuses: " << status;
      if (status.IsAborted()) {
        Complete(status);
      } else {
        Execute();
      }
      return;
    }

    if (response.has_propagated_hybrid_time()) {
      participant_context_.UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }

    auto it = queues_.begin();
    auto& queue = it->second;
    if ((response.status().size() != 1 && response.status().size() != request_size) ||
        (response.aborted_subtxn_set().size() != 0 && // Old node may not populate these.
            response.aborted_subtxn_set().size() != request_size)) {
      // Node with old software version would always return 1 status.
      LOG_WITH_PREFIX(DFATAL)
          << "Bad response size, expected " << request_size << " entries, but found: "
          << response.ShortDebugString() << ", queue: " << AsString(queues_);
      Execute();
      return;
    }

    status_infos_.clear();
    status_infos_.resize(response.status().size());
    for (int i = 0; i != response.status().size(); ++i) {
      auto& status_info = status_infos_[i];
      status_info.status_tablet = it->first;
      status_info.transaction_id = queue.front();
      status_info.status = response.status(i);
      if (response.deadlock_reason().size() > i &&
          response.deadlock_reason(i).code() != AppStatusPB::OK) {
        // response contains a deadlock specific error.
        status_info.expected_deadlock_status = StatusFromPB(response.deadlock_reason(i));
      }

      if (PREDICT_FALSE(response.aborted_subtxn_set().empty())) {
        YB_LOG_EVERY_N(WARNING, 1)
            << "Empty aborted_subtxn_set in transaction status response. "
            << "This should only happen when nodes are on different versions, e.g. during "
            << "upgrade.";
      } else {
        auto aborted_subtxn_set_or_status = SubtxnSet::FromPB(
          response.aborted_subtxn_set(i).set());
        if (!aborted_subtxn_set_or_status.ok()) {
          Complete(STATUS_FORMAT(
              IllegalState, "Cannot deserialize SubtxnSet: $0",
              response.aborted_subtxn_set(i).DebugString()));
          return;
        }
        status_info.aborted_subtxn_set = aborted_subtxn_set_or_status.get();
      }

      if (i < response.status_hybrid_time().size()) {
        status_info.status_ht = HybridTime(response.status_hybrid_time(i));
      // Could happen only when coordinator has an old version.
      } else if (status_info.status == TransactionStatus::ABORTED) {
        status_info.status_ht = HybridTime::kMax;
      } else {
        Complete(STATUS_FORMAT(
            IllegalState, "Missing status hybrid time for transaction status: $0",
            TransactionStatus_Name(status_info.status)));
        return;
      }
      status_info.coordinator_safe_time = i < response.coordinator_safe_time().size()
          ? HybridTime::FromPB(response.coordinator_safe_time(i)) : HybridTime();
      VLOG_WITH_PREFIX(4) << "Status: " << status_info.ToString();
      queue.pop_front();
    }

    if (queue.empty()) {
      VLOG_WITH_PREFIX(2) << "Processed queue for: " << it->first;
      queues_.erase(it);
    }

    callback_(status_infos_);

    Execute();
  }

  void Complete(const Status& status) {
    VLOG_WITH_PREFIX(2) << "Complete: " << status;
    result_promise_.set_value(status);
    AtomicFlagSleepMs(&FLAGS_TEST_inject_status_resolver_complete_delay_ms);
    run_latch_.CountDown();
  }

  TransactionParticipantContext& participant_context_;
  rpc::Rpcs& rpcs_;
  const int max_transactions_per_request_;
  TransactionStatusResolverCallback callback_;

  const std::string log_prefix_;
  rpc::Rpcs::Handle handle_;

  std::atomic<bool> closing_{false};
  CountDownLatch run_latch_{0};
  CoarseTimePoint deadline_;
  std::unordered_map<TabletId, std::deque<TransactionId>> queues_;
  std::vector<TransactionStatusInfo> status_infos_;
  std::promise<Status> result_promise_;
};

TransactionStatusResolver::TransactionStatusResolver(
    TransactionParticipantContext* participant_context, rpc::Rpcs* rpcs,
    int max_transactions_per_request, TransactionStatusResolverCallback callback)
    : impl_(new Impl(
        participant_context, rpcs, max_transactions_per_request, std::move(callback))) {
}

TransactionStatusResolver::~TransactionStatusResolver() {}

void TransactionStatusResolver::Shutdown() {
  impl_->Shutdown();
}

void TransactionStatusResolver::Add(
    const TabletId& status_tablet, const TransactionId& transaction_id) {
  impl_->Add(status_tablet, transaction_id);
}

void TransactionStatusResolver::Start(CoarseTimePoint deadline) {
  impl_->Start(deadline);
}

std::future<Status> TransactionStatusResolver::ResultFuture() {
  return impl_->ResultFuture();
}

bool TransactionStatusResolver::Running() const {
  return impl_->Running();
}

} // namespace tablet
} // namespace yb
