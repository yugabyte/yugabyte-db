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

#include "yb/tserver/cdc_table_poller.h"

#include <condition_variable>
#include <map>
#include <mutex>

#include "yb/ash/wait_state.h"

#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client.h"

#include "yb/common/wire_protocol.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/cdc_table_output_client.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"

DEFINE_RUNTIME_bool(cdc_table_poller_local_get_changes, true,
    "Whether the CDC table pollers call GetChanges on the CDC service of their own tserver "
    "directly. When false, they send it as a GetChangesRpc, which looks the tablet up in the meta "
    "cache.");

DEFINE_RUNTIME_bool(cdc_table_poller_skip_empty_apply, true,
    "Whether a CDC table poller polls again right away after a GetChanges response with no row "
    "change, instead of passing the response through the output client.");

DEFINE_RUNTIME_bool(cdc_table_poller_inline_get_changes, true,
    "Whether a CDC table poller runs its local GetChanges, and the handling of the response, in "
    "its own thread instead of handing them to the CDC service's and the RPC callback thread "
    "pools. Needs cdc_table_poller_local_get_changes.");

DEFINE_RUNTIME_bool(cdc_table_poller_batch_idle_timer, true,
    "Whether the CDC table pollers wait out their idle backoff on one shared timer thread, which "
    "wakes once per cdc_table_poller_idle_timer_tick_ms for all pollers due in that tick, instead "
    "of on a reactor timer each.");

DEFINE_RUNTIME_int32(cdc_table_poller_idle_timer_tick_ms, 5,
    "The idle backoff of a CDC table poller is rounded up to a multiple of this.");

DEFINE_RUNTIME_bool(cdc_table_poller_cache_stream_id, true,
    "Whether a CDC table poller formats its stream id once instead of on every GetChanges.");

DECLARE_int32(cdc_read_rpc_timeout_ms);

namespace yb::tserver {

namespace {

// Runs the idle backoff of the CDC table pollers. A reactor timer per poll costs a wakeup of the
// reactor to set it and another when it fires; this thread instead wakes once per tick for every
// poller due in it, and only the pollers that become the earliest due wake it to be added.
class IdlePollTimer {
 public:
  static IdlePollTimer& Instance() {
    // Never destroyed: pollers may schedule until the process exits.
    static auto* timer = new IdlePollTimer();
    return *timer;
  }

  void Schedule(int64_t delay_ms, std::weak_ptr<XClusterAsyncExecutor> poller) {
    const auto tick =
        std::chrono::milliseconds(std::max(1, FLAGS_cdc_table_poller_idle_timer_tick_ms));
    const auto since_epoch =
        (CoarseMonoClock::now() + std::chrono::milliseconds(delay_ms)).time_since_epoch();
    const auto due = CoarseTimePoint(std::chrono::duration_cast<CoarseMonoClock::duration>(
        (since_epoch + tick - CoarseMonoClock::duration(1)) / tick * tick));
    bool notify;
    {
      std::lock_guard l(mutex_);
      notify = due_.empty() || due < due_.begin()->first;
      due_.emplace(due, std::move(poller));
    }
    if (notify) {
      cv_.notify_one();
    }
  }

 private:
  IdlePollTimer() {
    CHECK_OK(Thread::Create("cdc_table", "idle_poll_timer", &IdlePollTimer::Run, this, &thread_));
  }

  void Run() {
    std::vector<std::weak_ptr<XClusterAsyncExecutor>> ready;
    std::unique_lock l(mutex_);
    for (;;) {
      if (due_.empty()) {
        cv_.wait(l);
        continue;
      }
      const auto now = CoarseMonoClock::now();
      if (now < due_.begin()->first) {
        cv_.wait_for(l, due_.begin()->first - now);
        continue;
      }
      const auto end = due_.upper_bound(now);
      for (auto it = due_.begin(); it != end; ++it) {
        ready.push_back(std::move(it->second));
      }
      due_.erase(due_.begin(), end);
      l.unlock();
      for (auto& weak_poller : ready) {
        if (auto poller = std::static_pointer_cast<CdcTablePoller>(weak_poller.lock())) {
          poller->PollAfterIdleDelay();
        }
      }
      ready.clear();
      l.lock();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::multimap<CoarseTimePoint, std::weak_ptr<XClusterAsyncExecutor>> due_;
  scoped_refptr<Thread> thread_;
};

}  // namespace

CdcTablePoller::CdcTablePoller(
    const xcluster::ProducerTabletInfo& producer_tablet_info,
    const xcluster::ConsumerTabletInfo& consumer_tablet_info, const NamespaceId& namespace_id,
    ThreadPool* thread_pool, rpc::Rpcs* rpcs, client::YBClient& local_client,
    XClusterConsumer* xcluster_consumer, int64_t leader_term,
    std::function<int64_t(const TabletId&)> get_leader_term, const std::string& ts_uuid,
    cdc::CDCStateTable& cdc_state_table)
    : XClusterPoller(
          producer_tablet_info, consumer_tablet_info, namespace_id,
          /* auto_flags_version= */ nullptr, thread_pool, rpcs, local_client,
          /* source_client= */ nullptr, xcluster_consumer, leader_term, std::move(get_leader_term),
          /* is_automatic_mode= */ false, /* is_stream_paused= */ false, ts_uuid),
      cdc_state_table_(cdc_state_table),
      stream_id_str_(producer_tablet_info.stream_id.ToString()),
      // The empty HostPort makes the proxy call the service of this tserver in process. If the
      // leader has moved, the CDC service forwards the call to it (serve_as_proxy).
      local_cdc_proxy_(
          std::make_shared<cdc::CDCServiceProxy>(&local_client.proxy_cache(), HostPort())) {}

void CdcTablePoller::InitCdcTablePoller(
    rocksdb::RateLimiter* rate_limiter,
    std::optional<std::unordered_set<TableId>> published_table_ids) {
  std::lock_guard l(data_mutex_);
  output_client_ = std::make_shared<CdcTableOutputClient>(
      this, consumer_tablet_info_.table_id, local_client_, rate_limiter,
      std::move(published_table_ids));
}

Result<CdcTablePoller::NextRequest> CdcTablePoller::ReadCheckpointFromCdcState() {
  auto entry = VERIFY_RESULT(cdc_state_table_.TryFetchEntry(
      {producer_tablet_info_.tablet_id, producer_tablet_info_.stream_id},
      cdc::CDCStateTableEntrySelector().IncludeCheckpoint().IncludeCDCSDKSafeTime()));
  // The stream is created with a checkpoint for each of its tablets.
  SCHECK(
      entry && entry->checkpoint && entry->checkpoint->valid(), IllegalState,
      "No checkpoint in cdc_state for tablet $0 of stream $1", producer_tablet_info_.tablet_id,
      producer_tablet_info_.stream_id);

  NextRequest next;
  next.checkpoint.set_term(entry->checkpoint->term);
  next.checkpoint.set_index(entry->checkpoint->index);
  next.checkpoint.set_key("");
  next.checkpoint.set_write_id(0);
  if (entry->cdc_sdk_safe_time) {
    next.safe_hybrid_time = *entry->cdc_sdk_safe_time;
  }
  return next;
}

Status CdcTablePoller::PrepareGetChangesRequest(cdc::GetChangesRequestPB* req) {
  if (!next_request_) {
    // Blocks, on a thread of the consumer's pool. Done once per poller.
    next_request_ = VERIFY_RESULT(ReadCheckpointFromCdcState());
    LOG_WITH_PREFIX(INFO) << "Polling from checkpoint "
                          << next_request_->checkpoint.ShortDebugString() << ", safe time "
                          << next_request_->safe_hybrid_time;
    // Only sent to the CDC service once the changes before it were in the CDC table.
    if (next_request_->safe_hybrid_time > 0) {
      UpdateSafeTime(HybridTime(next_request_->safe_hybrid_time));
    }
  }

  if (FLAGS_cdc_table_poller_cache_stream_id) {
    req->set_stream_id(stream_id_str_);
  } else {
    req->set_stream_id(producer_tablet_info_.stream_id.ToString());
  }
  req->set_tablet_id(producer_tablet_info_.tablet_id);
  // Until the consumer moves this poller after a leader change, the old leader's CDC service
  // forwards the call to the new leader.
  req->set_serve_as_proxy(true);
  // The CDC table keeps the values undecoded, which is how the CDC service sends them to a
  // walsender, along with the attribute number and type that decoding needs.
  req->set_cdcsdk_request_source(cdc::CDCSDKRequestSource::WALSENDER);

  *req->mutable_from_cdc_sdk_checkpoint() = next_request_->checkpoint;
  req->set_safe_hybrid_time(next_request_->safe_hybrid_time);
  req->set_wal_segment_index(next_request_->wal_segment_index);
  req->set_max_index_in_sort_window(next_request_->max_index_in_sort_window);

  // Everything before the checkpoint has been written to the CDC table, so the stream no longer
  // needs it. The CDC service ignores the key and write id of an explicit checkpoint.
  auto* explicit_checkpoint = req->mutable_explicit_cdc_sdk_checkpoint();
  explicit_checkpoint->set_term(next_request_->checkpoint.term());
  explicit_checkpoint->set_index(next_request_->checkpoint.index());
  if (next_request_->safe_hybrid_time >= 0) {
    explicit_checkpoint->set_snapshot_time(next_request_->safe_hybrid_time);
  }
  return Status::OK();
}

void CdcTablePoller::SendGetChangesRequest(
    client::YBClient* client, cdc::GetChangesRequestPB* req) {
  if (!FLAGS_cdc_table_poller_local_get_changes) {
    XClusterPoller::SendGetChangesRequest(client, req);
    return;
  }

  // A local call does not copy the request or the response: the caller owns them until the call
  // responds.
  struct Call {
    cdc::GetChangesRequestPB req;
    std::shared_ptr<cdc::GetChangesResponsePB> resp =
        std::make_shared<cdc::GetChangesResponsePB>();
    rpc::RpcController controller;
  };
  auto call = std::make_shared<Call>();
  call->req.Swap(req);
  call->controller.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
  // allow_local_calls_in_curr_thread stays false: the call must not run inline here, since
  // HandleGetChangesResponse takes data_mutex_, which DoPoll holds.

  SET_WAIT_STATUS(XCluster_WaitingForGetChanges);
  local_cdc_proxy_->GetChangesAsync(
      call->req, call->resp.get(), &call->controller, [weak_this = weak_from_this(), call] {
        auto shared_this = std::static_pointer_cast<CdcTablePoller>(weak_this.lock());
        if (shared_this) {
          shared_this->HandleGetChangesResponse(call->controller.status(), call->resp);
        }
      });
}

bool CdcTablePoller::ShouldSendGetChangesWithoutLock() {
  return FLAGS_cdc_table_poller_local_get_changes && FLAGS_cdc_table_poller_inline_get_changes;
}

void CdcTablePoller::SendGetChangesRequestWithoutLock(
    client::YBClient* client, cdc::GetChangesRequestPB* req) {
  auto resp = std::make_shared<cdc::GetChangesResponsePB>();
  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
  // The handler runs in this thread and responds before the call returns, unless it forwards the
  // call to a new leader, which this thread then waits for.
  controller.set_allow_local_calls_in_any_thread(true);
  SET_WAIT_STATUS(XCluster_WaitingForGetChanges);
  auto status = local_cdc_proxy_->GetChanges(*req, resp.get(), &controller);
  HandleGetChangesResponse(std::move(status), std::move(resp));
}

void CdcTablePoller::SchedulePollAfter(int64_t delay_ms) {
  if (delay_ms <= 0 || !FLAGS_cdc_table_poller_batch_idle_timer) {
    XClusterPoller::SchedulePollAfter(delay_ms);
    return;
  }
  IdlePollTimer::Instance().Schedule(delay_ms, weak_from_this());
}

bool CdcTablePoller::CanSkipApply(const cdc::GetChangesResponsePB& resp) const {
  if (!FLAGS_cdc_table_poller_skip_empty_apply) {
    return false;
  }
  // The ops that CdcTableOutputClient::AddWrites writes.
  for (const auto& record : resp.cdc_sdk_proto_records()) {
    switch (record.row_message().op()) {
      case cdc::RowMessage::INSERT: [[fallthrough]];
      case cdc::RowMessage::UPDATE: [[fallthrough]];
      case cdc::RowMessage::DELETE:
        return false;
      default:
        break;
    }
  }
  return true;
}

Status CdcTablePoller::ProcessGetChangesResponseError(const cdc::GetChangesResponsePB& resp) {
  if (resp.has_error()) {
    // TODO: tablet splits. On TABLET_SPLIT the children have to be polled from this tablet's
    // checkpoint. Until then this poller keeps failing with backoff.
    LOG_WITH_PREFIX(WARNING) << "GetChanges failed: code="
                             << cdc::CDCErrorPB::Code_Name(resp.error().code())
                             << ", status=" << resp.error().status().ShortDebugString();
    auto status = StatusFromPB(resp.error().status());
    return status.ok() ? STATUS(InternalError, "GetChanges failed without a status") : status;
  }
  SCHECK(resp.has_cdc_sdk_checkpoint(), NotFound, "GetChanges response has no checkpoint");
  return Status::OK();
}

void CdcTablePoller::ChangesApplied(const XClusterOutputClientResponse& response) {
  const auto& resp = *response.get_changes_response;
  DCHECK(next_request_);
  NextRequest next;
  next.checkpoint = resp.cdc_sdk_checkpoint();
  // snapshot_time is not used when streaming changes.
  next.checkpoint.clear_snapshot_time();
  // The CDC service returns only changes that commit after the safe time of the request, so it
  // must not go back.
  next.safe_hybrid_time =
      resp.has_safe_hybrid_time() ? resp.safe_hybrid_time() : next_request_->safe_hybrid_time;
  next.wal_segment_index = resp.wal_segment_index();
  next.max_index_in_sort_window = resp.max_index_in_sort_window();
  next_request_ = std::move(next);

  // Every change of this tablet that commits up to the safe time of the response is in the CDC
  // table now. The consumer publishes it, the master takes the minimum over the tablets of the
  // stream, and the walsender reads the CDC table up to that minimum.
  if (next_request_->safe_hybrid_time > 0) {
    UpdateSafeTime(HybridTime(next_request_->safe_hybrid_time));
  }
}

}  // namespace yb::tserver
