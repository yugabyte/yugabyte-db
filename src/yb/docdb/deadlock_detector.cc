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

#include "yb/docdb/deadlock_detector.h"

#include <atomic>
#include <memory>
#include <mutex>

#include "yb/client/transaction_rpc.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/strongly_typed_uuid.h"
#include "yb/util/tsan_util.h"
#include "yb/util/unique_lock.h"
#include "yb/util/yb_pg_errcodes.h"

using namespace std::placeholders;
using namespace std::literals;

DEFINE_UNKNOWN_int32(
    clear_active_probes_older_than_seconds, 60,
    "Interval with which to clear active probes tracked at a deadlock detector. This ensures that "
    "the memory used to track both created and forwarded probes does not grow unbounded. If this "
    "is too low, we may remove entries too aggressively and end up failing to report deadlocks.");
TAG_FLAG(clear_active_probes_older_than_seconds, hidden);
TAG_FLAG(clear_active_probes_older_than_seconds, advanced);

DEFINE_RUNTIME_int32(
    clear_deadlocked_txns_info_older_than_heartbeats, 10,
    "Minimum number of transaction heartbeat periods for which a deadlocked transaction's info is "
    "retained, after it has been reported to be aborted. This ensures the memory used to track "
    "info of deadlocked transactions does not grow unbounded.");
TAG_FLAG(clear_deadlocked_txns_info_older_than_heartbeats, hidden);
TAG_FLAG(clear_deadlocked_txns_info_older_than_heartbeats, advanced);

METRIC_DEFINE_event_stats(
    tablet, deadlock_size, "Deadlock size", yb::MetricUnit::kTransactions,
    "The number of transactions involved in detected deadlocks");
METRIC_DEFINE_event_stats(
    tablet, deadlock_probe_latency, "Deadlock probe latency", yb::MetricUnit::kMicroseconds,
    "The time it takes to complete the probe from a waiting transaction to all of its blockers.");
METRIC_DEFINE_gauge_uint64(
    tablet, deadlock_detector_waiters, "Num Waiting Txns", yb::MetricUnit::kTransactions,
    "The total number of waiting transactions tracked by one deadlock detector.");

DEFINE_test_flag(int32, sleep_amidst_iterating_blockers_ms, 0,
    "Time for which the thread sleeps in each iteration while looping over the computed wait-for "
    "probes and sending information to the waiters.");

DECLARE_uint64(transaction_heartbeat_usec);

namespace yb {
namespace tablet {

namespace {

YB_STRONGLY_TYPED_UUID(DetectorId);

using LocalProbeProcessorCallback = std::function<void(
    const Status&, const tserver::ProbeTransactionDeadlockResponsePB&)>;
using WaiterTxnTuple = std::tuple<
    const TransactionId, const std::string, std::shared_ptr<const WaiterData>>;

// Container class which supports efficiently fetching items uniquely indexed by probe_num as well
// as efficiently removing items which were added before a threshold time or which are associated
// with lower than a given probe_num.
template <class T>
class ProbeTracker {
 public:
  ProbeTracker() {
    VLOG(4) << "Creating ProbeTracker";
  }

  ~ProbeTracker() {
    VLOG(4) << "Destroying ProbeTracker";
  }

  void UpdateMinProbeNo(uint32_t min_probe_num) EXCLUDES(mutex_) {
    UniqueLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());

    auto it = probes_.begin();
    while (it != probes_.end() && it->first < min_probe_num) {
      it = probes_.erase(it);
    }

    min_probe_num_ = min_probe_num;
  }

  void Remove(uint32_t probe_num) EXCLUDES(mutex_) {
    UniqueLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());
    probes_.erase(probe_num);
  }

  std::shared_ptr<T> AddOrGet(uint32_t probe_num, T&& value) EXCLUDES(mutex_) {
    UniqueLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());
    if (probe_num < min_probe_num_) {
      return nullptr;
    }
    return probes_.try_emplace(
        probe_num, std::move(value), CoarseMonoClock::Now()).first->second.val;
  }

  std::shared_ptr<T> Get(uint32_t probe_num) const EXCLUDES(mutex_) {
    SharedLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());
    if (probe_num < min_probe_num_) {
      return nullptr;
    }
    auto it = probes_.find(probe_num);
    if (it == probes_.end()) {
      return nullptr;
    }
    return it->second.val;
  }

  int64_t GetSmallestProbeNo() const EXCLUDES(mutex_) {
    SharedLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());
    auto it = probes_.begin();
    if (it == probes_.end()) {
      return min_probe_num_;
    }
    return it->first;
  }

  uint64_t size() const EXCLUDES(mutex_) {
    SharedLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());
    return probes_.size();
  }

  int64_t RemoveEntriesOlderThan(CoarseTimePoint threshold) EXCLUDES(mutex_) {
    auto num_erased = 0;
    auto probe_num_watermark = 0u;
    UniqueLock<decltype(mutex_)> l(mutex_);
    DCHECK(IsFirstProbeNumValid());

    auto it = probes_.begin();
    while (it != probes_.end() && it->second.entry_time < threshold) {
      auto probe_num = it->first;
      num_erased++;
      DCHECK(probe_num_watermark < probe_num || probe_num == 0)
          << Format("$0 vs $1", probe_num_watermark, probe_num);
      probe_num_watermark = probe_num;
      it = probes_.erase(it);
    }

    if (probe_num_watermark > 0) {
      min_probe_num_ = std::max(min_probe_num_, probe_num_watermark + 1);
    }

    VLOG(4)
        << "Done removing old probes. Remaining: " << probes_.size()
        << " and min num " << min_probe_num_;

    return num_erased;
  }

 private:
  bool IsFirstProbeNumValid() const REQUIRES_SHARED(mutex_) {
    return probes_.size() == 0 || probes_.begin()->first >= min_probe_num_;
  }

  mutable rw_spinlock mutex_;

  struct ProbeInfo {
    ProbeInfo(T&& val_, CoarseTimePoint entry_time_):
        val(std::make_shared<T>(std::move(val_))), entry_time(entry_time_) {}
    std::shared_ptr<T> val;
    CoarseTimePoint entry_time;
  };

  std::map<uint32_t, ProbeInfo> probes_ GUARDED_BY(mutex_);

  uint32_t min_probe_num_ GUARDED_BY(mutex_) = 0;
};

class LocalProbeProcessor : public std::enable_shared_from_this<LocalProbeProcessor> {
 public:
  LocalProbeProcessor(
      const std::string& detector_log_prefix, const DetectorId& origin_detector_id,
      uint32_t probe_num, uint32_t min_probe_num, const TransactionId& waiter_id, rpc::Rpcs* rpcs,
      client::YBClient* client, scoped_refptr<EventStats> probe_latency)
      : detector_log_prefix_(detector_log_prefix), origin_detector_id_(origin_detector_id),
        waiter_(waiter_id), probe_num_(probe_num), min_probe_num_(min_probe_num), rpcs_(rpcs),
        client_(client), probe_latency_(std::move(probe_latency)) {
          DCHECK_GE(probe_num_, min_probe_num_);
        }

  const std::string LogPrefix() const {
    return Format("$0- probe($1, $2) ", detector_log_prefix_, origin_detector_id_, probe_num_);
  }

  void AddBlocker(const BlockerTransactionInfo& blocker_info) {
    auto& blocker_id = blocker_info.id;
    auto& blocker_status_tablet = blocker_info.status_tablet;
    auto& blocking_subtxn_info = blocker_info.blocking_subtxn_info;
    handles_.push_back(rpcs_->Prepare());
    auto handle = handles_.back();
    if (handle == rpcs_->InvalidHandle()) {
      LOG_WITH_PREFIX_AND_FUNC(WARNING) << "Shutting down. Cannot send probe.";
      return;
    }

    tserver::ProbeTransactionDeadlockRequestPB req;
    req.set_detector_uuid(origin_detector_id_.data(), origin_detector_id_.size());
    req.set_probe_num(probe_num_);
    req.set_min_probe_num(min_probe_num_);
    req.set_waiting_txn_id(waiter_.data(), waiter_.size());
    req.set_blocking_txn_id(blocker_id.data(), blocker_id.size());
    req.set_tablet_id(blocker_status_tablet);
    *req.mutable_blocking_subtxn_set() = blocking_subtxn_info->pb();

    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "waiting_txn_id: " << waiter_ << ", "
        << "blocking_txn_id: " << blocker_id << ", "
        << "blocking_subtxn(s): " << blocking_subtxn_info->ToString() << ", "
        << "blocker_status_tablet: " << blocker_status_tablet << ", "
        << "probe_num: " << probe_num_ << ", "
        << "min_probe_num: " << min_probe_num_;

    auto wrapped_callback = [instance = shared_from_this(), handle](
        const auto& status, const auto& req, const auto& resp) {
      instance->callback(status, req, resp);
      instance->rpcs_->Unregister(handle);
    };

    *handle = client::ProbeTransactionDeadlock(
        TransactionRpcDeadline(),
        nullptr /* tablet*/,
        client_,
        &req,
        wrapped_callback);;
  }

  void Send() {
    if (handles_.size() == 0 && CanTrySendResponse()) {
      LOG_WITH_PREFIX(DFATAL) << "Tried sending probes to 0 remote blockers.";
      callback_(Status::OK(), tserver::ProbeTransactionDeadlockResponsePB());
      return;
    }

    VLOG_WITH_PREFIX(4) << "Sending " << handles_.size() << " probes";

    remaining_requests_ = handles_.size();
    if (probe_latency_) {
      sent_at_ = CoarseMonoClock::Now();
    }
    for (auto& handle : handles_) {
      (**handle).SendRpc();
    }
  }

  void SetCallback(LocalProbeProcessorCallback&& callback) {
    callback_ = std::move(callback);
  }

  bool CanTrySendResponse() {
    bool expected = false;
    return did_send_response_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
  }

  void callback(
      const Status& status, const tserver::ProbeTransactionDeadlockRequestPB& req,
      const tserver::ProbeTransactionDeadlockResponsePB& resp) EXCLUDES(mutex_) {
    auto remaining_requests = remaining_requests_.fetch_sub(1) - 1;
    if (remaining_requests < 0 || did_send_response_) {
      return;
    }

    if (resp.deadlocked_txn_ids_size() > 0) {
      if (CanTrySendResponse()) {
        InvokeCallback(Status::OK(), resp);
      }
      return;
    }

    if (!status.ok()) {
      UniqueLock<decltype(mutex_)> l(mutex_);
      if (s_.ok()) {
        s_ = status;
        resp_ = resp;
      }
    }

    if (remaining_requests == 0) {
      if (CanTrySendResponse()) {
        Status s = Status::OK();
        tserver::ProbeTransactionDeadlockResponsePB resp;
        {
          SharedLock<decltype(mutex_)> l(mutex_);
          s = s_;
          resp = resp_;
        }
        InvokeCallback(s, resp);
      }
    }
  }

  void InvokeCallback(const Status& s, const tserver::ProbeTransactionDeadlockResponsePB& resp) {
    LOG_IF(DFATAL, !did_send_response_)
        << "Invoking callback without checking that it was not already invoked.";
    if (probe_latency_) {
      probe_latency_->Increment(std::chrono::duration_cast<std::chrono::microseconds>(
          CoarseMonoClock::Now() - sent_at_).count());
    }
    callback_(s, resp);
  }

 private:
  const std::string& detector_log_prefix_;
  const DetectorId& origin_detector_id_;
  const TransactionId& waiter_;
  uint32_t probe_num_;
  uint32_t min_probe_num_;
  rpc::Rpcs* rpcs_;
  client::YBClient* client_;
  scoped_refptr<EventStats> probe_latency_;

  CoarseTimePoint sent_at_;

  std::vector<rpc::Rpcs::Handle> handles_;

  LocalProbeProcessorCallback callback_ = [](const auto& status, const auto& resp) {
    DCHECK(false) << "Did not set callback before sending probes.";
  };

  std::atomic<uint64> remaining_requests_;
  std::atomic<bool> did_send_response_ = false;

  mutable rw_spinlock mutex_;
  Status s_ GUARDED_BY(mutex_) = Status::OK();
  tserver::ProbeTransactionDeadlockResponsePB resp_ GUARDED_BY(mutex_);
};

using LocalProbeProcessorPtr = std::shared_ptr<LocalProbeProcessor>;

} // namespace

std::string ConstructDeadlockedMessage(const TransactionId& waiter,
                                       const tserver::ProbeTransactionDeadlockResponsePB& resp) {
  std::stringstream ss;
  ss << Format("Transaction $0 aborted due to a deadlock.\n$0", waiter.ToString());
  for (auto i = 1 ; i < resp.deadlocked_txn_ids_size() ; i++) {
    auto id_or_status = FullyDecodeTransactionId(resp.deadlocked_txn_ids(i));
    if (!id_or_status.ok()) {
      ss << Format(" -> [Error decoding txn id: $0]", id_or_status.status());
    } else {
      ss << Format(" -> $0", *id_or_status);
    }
  }
  ss << Format(" -> $0 ", waiter.ToString());
  return ss.str();
}

class DeadlockDetector::Impl : public std::enable_shared_from_this<DeadlockDetector::Impl> {
 public:
  explicit Impl(
      const std::shared_future<client::YBClient*>& client_future,
      TransactionAbortController* controller, const TabletId& status_tablet_id,
      const MetricEntityPtr& metrics)
      : client_future_(client_future), controller_(controller),
        detector_id_(DetectorId::GenerateRandom()),
        log_prefix_(Format("T $0 D $1 ", status_tablet_id, detector_id_)),
        deadlock_size_(METRIC_deadlock_size.Instantiate(metrics)),
        probe_latency_(METRIC_deadlock_probe_latency.Instantiate(metrics)),
        deadlock_detector_waiters_(METRIC_deadlock_detector_waiters.Instantiate(metrics, 0)) {
    VLOG_WITH_PREFIX(4) << "Deadlock detector started with instance id: " << detector_id_;
  }

  ~Impl() {
    Shutdown();
  }

  void Shutdown() {
    rpcs_.Shutdown();
  }

  void ProcessProbe(
      const tserver::ProbeTransactionDeadlockRequestPB& req,
      tserver::ProbeTransactionDeadlockResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback) {
    VLOG_WITH_PREFIX(4) << "Processing probe request " << req.ShortDebugString();

    auto processor_or_status = GetProbesToForward(req, resp);
    if (!processor_or_status.ok()) {
      callback(processor_or_status.status());
      return;
    }
    if (!*processor_or_status) {
      callback(Status::OK());
      return;
    }
    auto& processor = *processor_or_status;

    processor->SetCallback(
        [callback = std::move(callback), detector = shared_from_this(), req, resp]
        (const auto& status, const auto& remote_resp) {
      if (remote_resp.deadlocked_txn_ids_size() > 0) {
        *resp->mutable_deadlocked_txn_ids() = remote_resp.deadlocked_txn_ids();
        resp->add_deadlocked_txn_ids(req.blocking_txn_id());
        callback(Status::OK());
      } else {
        callback(status);
      }
#ifndef NDEBUG
      auto detector_id_or_status = FullyDecodeDetectorId(req.detector_uuid());
      if (!detector_id_or_status.ok()) {
        LOG(DFATAL) << detector_id_or_status->ToString();
        return;
      }
      auto probe_num = req.probe_num();
      UniqueLock<decltype(detector->mutex_)> l(detector->mutex_);
      auto it = detector->forwarded_probes_.find(*detector_id_or_status);
      if (it == detector->forwarded_probes_.end()) {
        LOG(DFATAL) << "Not found";
        return;
      }
      auto set = it->second->Get(probe_num);
      if (!set) {
        LOG(WARNING) << "Returned processing probe with no metadata";
        return;
      }
#endif
    });
    processor->Send();
  }

  void ProcessWaitFor(
      const tserver::UpdateTransactionWaitingForStatusRequestPB& req,
      tserver::UpdateTransactionWaitingForStatusResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback) {
    std::vector<WaiterTxnTuple> waiters_to_probe;
    auto status = [this, &waiters_to_probe](const auto& req) -> Status {
      UniqueLock<decltype(mutex_)> l(mutex_);
      auto tserver_uuid = req.tserver_uuid();
      RSTATUS_DCHECK(
          !tserver_uuid.empty(), InvalidArgument,
          Format("Got empty tserver_uuid in request $0", req.ShortDebugString()));

      // Erase exisiting wait-for dependencies from the Tablet Server in case of full update
      if (req.is_full_update()) {
        VLOG_WITH_PREFIX(1) << "Full Update received. Erasing exisiting wait-for dependencies from "
            << "TS: " << tserver_uuid;
        waiters_.get<TserverUuidTag>().erase(tserver_uuid);
      }

      for (const auto& waiter : req.waiting_transactions()) {
        auto waiter_txn_id = VERIFY_RESULT(FullyDecodeTransactionId(waiter.transaction_id()));
        auto waiter_txn_key = std::make_pair(waiter_txn_id, tserver_uuid);
        if (waiter.blocking_transaction_size() == 0) {
          LOG_WITH_PREFIX(WARNING) << "Received WaitFor relationship for waiter " << waiter_txn_id
                                   << " with no blockers";
          continue;
        }
        auto wait_start_time = HybridTime::FromPB(waiter.wait_start_time());
        VLOG_WITH_PREFIX(4) << "Processing waiter " << waiter_txn_id;

        std::shared_ptr<WaiterData> waiter_data = nullptr;
        auto waiter_it = waiters_.find(waiter_txn_key);
        if (waiter_it != waiters_.end()) {
          auto existing_waiter_start_time = waiter_it->waiter_data()->wait_start_time;
          if (existing_waiter_start_time == wait_start_time) {
            VLOG_WITH_PREFIX(1) << "Skipping stored waiter " << waiter_txn_id
                                << " with start time " << wait_start_time;
            continue;
          } else if (existing_waiter_start_time > wait_start_time) {
            VLOG_WITH_PREFIX(1) << "Skipping stored waiter " << waiter_txn_id
                                << " with earlier start time " << existing_waiter_start_time
                                << " than request " << wait_start_time;
            continue;
          }
          VLOG_WITH_PREFIX(1) << "Overwriting stored waiter " << waiter_txn_id
                              << " from " << existing_waiter_start_time
                              << " with newer request at " << wait_start_time;
          waiter_data = std::make_shared<WaiterData>(WaiterData{
            .wait_start_time = std::move(wait_start_time),
            .blockers = std::make_shared<BlockerData>(BlockerData())
          });
          // waiters_ map is guarded by mutex_, hence resetting the value field (shared_ptr)
          // is thread safe. Copies of the shared_ptr that might operate outside the scope of
          // mutex_ continue to work on older objects.
          waiters_.modify(waiter_it, [&waiter_data](WaiterInfoEntry& entry) {
            entry.ResetWaiterData(waiter_data);
          });
        } else {
          VLOG_WITH_PREFIX(1) << "Creating new stored waiter " << waiter_txn_id
                              << " with start time " << wait_start_time;
          waiter_data = std::make_shared<WaiterData>(WaiterData {
            .wait_start_time = std::move(wait_start_time),
            .blockers = std::make_shared<BlockerData>(BlockerData()),
          });
          auto it = waiters_.emplace(
                WaiterInfoEntry(waiter_txn_id, tserver_uuid, waiter_data));
          DCHECK(it.second);
          waiter_it = it.first;
        }

        auto& blockers = DCHECK_NOTNULL(waiter_data)->blockers;
        blockers->reserve(waiter.blocking_transaction_size());
        for (const auto& blocker : waiter.blocking_transaction()) {
          if (blocker.status_tablet_id().empty()) {
            LOG_WITH_PREFIX_AND_FUNC(DFATAL)
                << "Got empty status tablet in request " << waiter.ShortDebugString();
            continue;
          }

          // TODO(wait-queues): SubtxnSetAndPB::Create internally copies the passed in SubtxnSetPB
          // object. Check if we can avoid the copy and use std::move on the proto subfield instead.
          blockers->push_back(BlockerTransactionInfo {
            .id = VERIFY_RESULT(FullyDecodeTransactionId(blocker.transaction_id())),
            .status_tablet = blocker.status_tablet_id(),
            .blocking_subtxn_info = VERIFY_RESULT(SubtxnSetAndPB::Create(blocker.subtxn_set())),
          });
          const BlockerTransactionInfo& blocker_txn = blockers->back();
          VLOG_WITH_PREFIX(4)
              << "Adding new wait-for relationship --"
              << "blocker txn id: " << blocker_txn.id << " "
              << "blocker status tablet: " << blocker_txn.status_tablet << " "
              << "blocking subtxn(s): " << blocker_txn.blocking_subtxn_info->ToString() << " "
              << "waiter txn id: " << waiter_txn_id << " "
              << "received from TS: " << tserver_uuid << " "
              << "start time: " << waiter_data->wait_start_time;
        }
        // TODO(wait-queues): Tracking tserver uuid here is unnecessary as it isn't required in
        // GetProbesToSend. We adhere to this format so that GetProbesToSend function can be re-used
        // for both 'waiters_'  as well as 'waiters_to_probe'.
        waiters_to_probe.push_back(
            {waiter_it->txn_id(), "" /* tserver uuid */, waiter_it->waiter_data()});
      }
      return Status::OK();
    }(req);

    if (!status.ok()) {
      callback(status);
      return;
    }

    callback(Status::OK());
    for (const auto& probe : GetProbesToSend(waiters_to_probe)) {
      probe->Send();
    }
  }

  void TriggerProbes() EXCLUDES(mutex_) {
    // We should be able to trigger probes only once per unique waiting transaction, but we still
    // trigger all active probes on a fixed interval for safetey/simplicity.
    std::vector<LocalProbeProcessorPtr> probes_to_send;
    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      controller_->RemoveInactiveTransactions(&waiters_);
      deadlock_detector_waiters_->set_value(waiters_.size());
      if (is_probe_scan_active_) {
        return;
      }
      is_probe_scan_active_ = true;
      // TODO(wait-queues): Trigger probes only for waiters which which have
      // wait_start_time > Now() - N seconds
      probes_to_send = GetProbesToSend(waiters_);

      // Clear the info of old deadlocked transactions.
      auto interval = FLAGS_clear_deadlocked_txns_info_older_than_heartbeats *
          FLAGS_transaction_heartbeat_usec * 1us;
      auto expired_cutoff_time = CoarseMonoClock::Now() - interval;
      for (auto it = recently_deadlocked_txns_info_.begin();
           it != recently_deadlocked_txns_info_.end();) {
        const auto& deadlock_time = it->second.second;
        if (deadlock_time < expired_cutoff_time) {
          it = recently_deadlocked_txns_info_.erase(it);
        } else {
          it++;
        }
      }
    }

    for (auto& processor : probes_to_send) {
      processor->Send();
    }

    auto threshold = CoarseMonoClock::Now() - FLAGS_clear_active_probes_older_than_seconds * 1s;
    auto removed_local_probes = created_probes_.RemoveEntriesOlderThan(threshold);
    VLOG_WITH_PREFIX(1) << "Removed " << removed_local_probes << " old probes from created_probes_";
    {
      SharedLock<decltype(mutex_)> l(mutex_);
      for (const auto& [detector_id, forwarded_probes] : forwarded_probes_) {
        auto removed_forwarded_probes = forwarded_probes->RemoveEntriesOlderThan(threshold);
        VLOG_WITH_PREFIX(1) << "Removed " << removed_forwarded_probes
                            << " old probes from tracking for remote detector " << detector_id;
      }
    }
  }

  Status GetTransactionDeadlockStatus(const TransactionId& txn_id) {
    SharedLock<decltype(mutex_)> l(mutex_);
    auto it = recently_deadlocked_txns_info_.find(txn_id);
    if (it == recently_deadlocked_txns_info_.end()) {
      return Status::OK();
    }
    // Return Expired so that TabletInvoker does not retry. Also, query layer proactively sends
    // clean up requests to transaction participant only for transactions with Expired status.
    return STATUS_EC_FORMAT(
        Expired, TransactionError(TransactionErrorCode::kDeadlock), it->second.first);
  }

 private:
  template <class T>
  std::vector<LocalProbeProcessorPtr> GetProbesToSend(const T& waiters) {
    std::vector<LocalProbeProcessorPtr> probes_to_send;
    std::shared_ptr<std::atomic<uint64>> outstanding_probes =
        std::make_shared<std::atomic<uint64>>(waiters.size());

    // A waiter_txn_id might be encountered multiple times in the below iterations depending
    // on the number of Tablet Servers the wait-for dependencies for the waiter arrived from.
    //
    // TODO(wait-queues): Coalesce multiple entries in waiters with the same waiter_txn_id into a
    // single LocalProbeProcessor.
    for (const auto& [waiter_txn_id, _, waiter_data] : waiters) {
      if (waiter_data->blockers->empty()) {
        LOG_WITH_PREFIX(WARNING) << "Tried getting probes for waiter with no blockers "
                                 << waiter_txn_id;
        continue;
      }
      // We need to call created_probes_.GetSmallestProbeNo() before seq_no_.fetch_add(1) to avoid a
      // race condition wherein one thread grabs a lower probe_num from seq_no but calls
      // GetSmallestProbeNo after another thread which grabbed a higher probe_num from seq_no. If we
      // computed these values in reverse order, we could run into a situation where we end up with
      // probe_num < min_probe_num, which violates a key invariant of the local ProbeTracker.
      auto min_probe_num = created_probes_.GetSmallestProbeNo();
      auto probe_num = seq_no_.fetch_add(1);
      auto processor = std::make_shared<LocalProbeProcessor>(
          log_prefix_, detector_id_, probe_num, min_probe_num,
          waiter_txn_id, &rpcs_, &client(), probe_latency_);
      for (const auto& blocker : *waiter_data->blockers) {
        AtomicFlagSleepMs(&FLAGS_TEST_sleep_amidst_iterating_blockers_ms);
        DCHECK(!blocker.status_tablet.empty());
        processor->AddBlocker(blocker);
      }
      processor->SetCallback([detector = shared_from_this(), outstanding_probes, probe_num]
          (const auto& status, const auto& resp) {
        VLOG(4) << "Got callback for probe "
                << Format("($0, $1)", probe_num, detector->detector_id_);
        detector->created_probes_.Remove(probe_num);
        if (outstanding_probes->fetch_sub(1) == 1) {
          UniqueLock<decltype(mutex_)> l(detector->mutex_);
          detector->is_probe_scan_active_ = false;
        }
        if (resp.deadlocked_txn_ids_size() > 0) {
          detector->deadlock_size_->Increment(resp.deadlocked_txn_ids_size());
          auto waiter_or_status = FullyDecodeTransactionId(resp.deadlocked_txn_ids(0));
          if (!waiter_or_status.ok()) {
            LOG(ERROR) << "Failed to decode transaction id in detected deadlock!";
          } else {
            const auto& waiter = *waiter_or_status;
            {
              UniqueLock<decltype(detector->mutex_)> l(detector->mutex_);
              auto deadlock_info = std::make_pair(ConstructDeadlockedMessage(waiter, resp),
                                                  CoarseMonoClock::Now());
              detector->recently_deadlocked_txns_info_.emplace(waiter, deadlock_info);
            }
            detector->controller_->Abort(
                waiter, std::bind(&DeadlockDetector::Impl::TxnAbortCallback, detector, _1, waiter));
          }
        }
      });
      probes_to_send.push_back(processor);
      created_probes_.AddOrGet(probe_num, TransactionId(waiter_txn_id));
    }
    return probes_to_send;
  }

  std::vector<std::shared_ptr<const BlockerData>> GetBlockersUnlocked(
      const DetectorId& detector_id, uint32_t probe_num, const TransactionId& blocking_txn_id)
      REQUIRES_SHARED(mutex_) {
    std::vector<std::shared_ptr<const BlockerData>> blockers;
    auto waiter_entries =
        boost::make_iterator_range(waiters_.get<TransactionIdTag>().equal_range(blocking_txn_id));
    for (auto entry : waiter_entries) {
      if (entry.waiter_data()->blockers->empty()) {
        LOG_WITH_PREFIX(DFATAL)
            << "Found empty blockers list while processing probe from "
            << "detector  " << detector_id.ToString() << " "
            << "with probe_num " << probe_num << " "
            << "and blocking_txn " << blocking_txn_id;
      }
      blockers.push_back(entry.waiter_data()->blockers);
    }

    return blockers;
  }

  Result<LocalProbeProcessorPtr> GetProbesToForward(
      const tserver::ProbeTransactionDeadlockRequestPB& req,
      tserver::ProbeTransactionDeadlockResponsePB* resp) {
    auto detector_id = VERIFY_RESULT(FullyDecodeDetectorId(req.detector_uuid()));
    auto probe_num = req.probe_num();
    auto waiting_txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.waiting_txn_id()));
    auto blocking_txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.blocking_txn_id()));

    auto blocking_subtxn_set = VERIFY_RESULT(SubtxnSet::FromPB(req.blocking_subtxn_set().set()));
    auto blocking_subtxn_active =
        controller_->IsAnySubtxnActive(blocking_txn_id, blocking_subtxn_set);
    // If no subtxn of the blocker txn's blocking_subtxn_set is active, drop the probe.
    if (!blocking_subtxn_active) {
      LOG_WITH_PREFIX_AND_FUNC(INFO)
              << "Dropping probe_num: " << probe_num << ", waiter: " << waiting_txn_id
              << ", blocked on: " << blocking_txn_id << " with inactive/aborted"
              << " subtxns:" << yb::ToString(blocking_subtxn_set) << ".";
      return nullptr;
    }

    std::vector<std::shared_ptr<const BlockerData>> blockers_per_ts;
    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      if (detector_id == detector_id_) {
        // Detector has received back the probe that originated from it, could lead to one of
        // the following -
        // 1. can't find the probe entry that was created, hence no point in forwarding the probe.
        // 2. received blocker_txn == waiter_txn that initiated the probe, implies a deadlock.
        // 3. recevied blocker_txn != waiter_txn that initiated the probe, the probes needs to be
        //    forwarded so as to detect local deadlocks (deadlock due to txns maintained by this
        //    coordinator itself).
        // Note: The above scenarios only hold true when the received blocker_txn has at least one
        //       active subtxn in its blocking_subtxn_set.
        auto probe_originating_txn = created_probes_.Get(probe_num);
        if (!probe_originating_txn) {
          LOG_WITH_PREFIX_AND_FUNC(INFO) << "Did not find probe_num: " << probe_num;
          return nullptr;
        }
        if (*probe_originating_txn == blocking_txn_id) {
          LOG_WITH_PREFIX_AND_FUNC(INFO)
              << "Found deadlock: probe_num: " << probe_num << ", waiter: " << waiting_txn_id
              << ", blocked on: " << blocking_txn_id
              << " with subtxn(s): " << yb::ToString(blocking_subtxn_set);
          resp->add_deadlocked_txn_ids(req.blocking_txn_id());
          return nullptr;
        }
        LOG_WITH_PREFIX_AND_FUNC(INFO)
            << "Found probe_num " << probe_num
            << " with different transaction_id "<< *probe_originating_txn << " "
            << "than blocker " << blocking_txn_id << ". Not marking as deadlock.";
      }

      auto processing_it = forwarded_probes_.emplace(
          detector_id, std::make_shared<ProbeTracker<TransactionIdSet>>());
      if (processing_it.second) {
        VLOG_WITH_PREFIX(4) << "Creating new probe tracker for detector " << detector_id;
      } else {
        VLOG_WITH_PREFIX(4) << "Reusing old probe tracker for detector " << detector_id;
      }
      auto& tracker = processing_it.first->second;
      DCHECK_GE(probe_num, req.min_probe_num());
      tracker->UpdateMinProbeNo(req.min_probe_num());

      auto seen_blockers = tracker->AddOrGet(probe_num, {});
      if (!seen_blockers) {
        VLOG_WITH_PREFIX_AND_FUNC(1)
            << "Dropping probe with too-small probe_num " << probe_num
            << " from detector " << detector_id;
        return nullptr;
      }

      auto blocking_it = seen_blockers->emplace(blocking_txn_id);
      if (!blocking_it.second) {
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Dropping already seen probe"
                << " from detector " << detector_id
                << " with probe_num " << probe_num
                << " at detector " << detector_id_;
        return nullptr;
      } else {
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Tracking probe"
                << " from detector " << detector_id
                << " with probe_num " << probe_num
                << " from waiter " << waiting_txn_id
                << " to blocker " << blocking_txn_id
                << " at detector " << detector_id_;
      }

      blockers_per_ts = GetBlockersUnlocked(detector_id, probe_num, blocking_txn_id);
    }
    if (blockers_per_ts.empty()) {
      return nullptr;
    }

    auto local_processor = std::make_shared<LocalProbeProcessor>(
        log_prefix_, detector_id, probe_num, req.min_probe_num(), waiting_txn_id, &rpcs_,
        &client(), nullptr /* probe_latency */);

    for (const auto& blockers : blockers_per_ts) {
      for (const auto& blocker : *blockers) {
        local_processor->AddBlocker(blocker);
      }
    }

    return local_processor;
  }

  void TxnAbortCallback(Result<TransactionStatusResult> res, const TransactionId txn_id) {
    if (res.ok()) {
      if (res->status == TransactionStatus::ABORTED) {
        LOG_WITH_FUNC(INFO) << "Aborting deadlocked transaction " << txn_id << " succeeded.";
        return;
      }
      LOG_WITH_FUNC(INFO) << "Aborting deadlocked transaction " << txn_id
                          << " failed -- status: " << res->status << ", time: " << res->status_time;
    } else {
      LOG_WITH_FUNC(INFO) << "Aborting deadlocked transaction " << txn_id
                          << " failed -- " << res.status();
    }
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  client::YBClient& client() { return *client_future_.get(); }

  const std::shared_future<client::YBClient*>& client_future_;
  TransactionAbortController* const controller_;
  const DetectorId detector_id_;
  const std::string log_prefix_;

  scoped_refptr<EventStats> deadlock_size_;
  scoped_refptr<EventStats> probe_latency_;
  scoped_refptr<AtomicGauge<uint64_t>> deadlock_detector_waiters_;

  mutable rw_spinlock mutex_;

  rpc::Rpcs rpcs_;

  std::unordered_map<DetectorId,
                     std::shared_ptr<ProbeTracker<TransactionIdSet>>,
                     boost::hash<DetectorId>> forwarded_probes_ GUARDED_BY(mutex_);

  ProbeTracker<TransactionId> created_probes_;

  bool is_probe_scan_active_ GUARDED_BY(mutex_) = false;

  Waiters waiters_ GUARDED_BY(mutex_);

  std::atomic<uint32_t> seq_no_ = 0;

  std::unordered_map<TransactionId, std::pair<std::string, CoarseTimePoint>, TransactionIdHash>
      recently_deadlocked_txns_info_ GUARDED_BY(mutex_);
};

DeadlockDetector::DeadlockDetector(
    const std::shared_future<client::YBClient*>& client_future,
    TransactionAbortController* controller,
    const TabletId& status_tablet_id,
    const MetricEntityPtr& metrics):
  impl_(new Impl(client_future, controller, status_tablet_id, metrics)) {}

DeadlockDetector::~DeadlockDetector() {}

void DeadlockDetector::ProcessProbe(
    const tserver::ProbeTransactionDeadlockRequestPB& req,
    tserver::ProbeTransactionDeadlockResponsePB* resp,
    DeadlockDetectorRpcCallback&& callback) {
  return impl_->ProcessProbe(req, resp, std::move(callback));
}

void DeadlockDetector::ProcessWaitFor(
    const tserver::UpdateTransactionWaitingForStatusRequestPB& req,
    tserver::UpdateTransactionWaitingForStatusResponsePB* resp,
    DeadlockDetectorRpcCallback&& callback) {
  return impl_->ProcessWaitFor(req, resp, std::move(callback));
}

void DeadlockDetector::TriggerProbes() {
  return impl_->TriggerProbes();
}

Status DeadlockDetector::GetTransactionDeadlockStatus(const TransactionId& txn_id) {
  return impl_->GetTransactionDeadlockStatus(txn_id);
}

void DeadlockDetector::Shutdown() {
  return impl_->Shutdown();
}

} // namespace tablet
} // namespace yb
