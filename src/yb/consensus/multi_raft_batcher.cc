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

#include "yb/consensus/multi_raft_batcher.h"
#include <memory>
#include <thread>

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/rpc/periodic.h"

#include "yb/util/dist_trace.h"
#include "yb/util/memory/arena.h"
#include "yb/util/flags.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_bool(enable_multi_raft_heartbeat_batcher, false,
            "If true, enables multi-Raft batching of raft heartbeats.");

DEFINE_RUNTIME_bool(multi_raft_batch_data_requests, false,
    "Send Raft requests that carry operations through the per-tablet-server multi-Raft batcher, "
    "so that the requests of all tablets led by this server to the same follower server go out "
    "as one MultiRaftUpdateConsensus RPC. Applies to peers created after it is set. Also requires "
    "--enable_batched_tserver_rpcs.");

DEFINE_RUNTIME_uint32(multi_raft_batch_max_in_flight, 2,
    "Maximum number of multi-Raft batches in flight to one tablet server before a batch with "
    "operations waits for one of them to return.");

DEFINE_UNKNOWN_uint64(multi_raft_heartbeat_interval_ms, 50,
              "The heartbeat interval for batch Raft replication.");
TAG_FLAG(multi_raft_heartbeat_interval_ms, advanced);

DEFINE_UNKNOWN_uint64(multi_raft_batch_size, 0,
              "Maximum batch size for a multi-Raft consensus payload. Ignored if set to zero.");
TAG_FLAG(multi_raft_batch_size, advanced);

DECLARE_int32(consensus_rpc_timeout_ms);
DECLARE_bool(enable_batched_tserver_rpcs);

namespace yb {
namespace consensus {

using rpc::PeriodicTimer;

struct MultiRaftHeartbeatBatcher::MultiRaftConsensusData {
  MultiRaftConsensusData()
      : batch_req(arena.NewObject<LWMultiRaftConsensusRequestPB>(&arena)),
        batch_res(arena.NewObject<LWMultiRaftConsensusResponsePB>(&arena)) {}

  ThreadSafeArena arena;
  LWMultiRaftConsensusRequestPB* batch_req;
  LWMultiRaftConsensusResponsePB* batch_res;
  rpc::RpcController controller;
  std::vector<MultiRaftResponseCallback> callbacks;
  // Keep the operations of data requests alive while the batch exists.
  std::vector<LWReplicateMsgsHolder> msgs_holders;
  bool has_data = false;
};

MultiRaftHeartbeatBatcher::MultiRaftHeartbeatBatcher(
    const HostPort& hostport,
    rpc::ProxyCache* proxy_cache,
    rpc::Messenger* messenger,
    std::atomic<int>* running_calls)
    : messenger_(messenger),
      consensus_proxy_(std::make_unique<ConsensusServiceProxy>(proxy_cache, hostport)),
      current_batch_(std::make_shared<MultiRaftConsensusData>()),
      running_calls_(running_calls) {}

void MultiRaftHeartbeatBatcher::Start() {
  std::weak_ptr<MultiRaftHeartbeatBatcher> weak_self = shared_from_this();
  batch_sender_ = PeriodicTimer::Create(
    messenger_,
    [weak_self]() {
      if (auto self = weak_self.lock()) {
        self->PrepareAndSendBatchRequest();
      }
    },
    MonoDelta::FromMilliseconds(FLAGS_multi_raft_heartbeat_interval_ms));
  batch_sender_->Start();
}

MultiRaftHeartbeatBatcher::~MultiRaftHeartbeatBatcher() {
  std::lock_guard lock(mutex_);
  LOG_IF(DFATAL, current_batch_ && !current_batch_->callbacks.empty())
      << "Not empty batch in ~MultiRaftHeartbeatBatcher";
}

void MultiRaftHeartbeatBatcher::AddHeartbeatToBatch(
    const LWConsensusRequestPB& request, MultiRaftResponseCallback callback) {
  std::shared_ptr<MultiRaftConsensusData> data = nullptr;
  {
    std::lock_guard lock(mutex_);
    current_batch_->callbacks.push_back(std::move(callback));
    current_batch_->batch_req->add_consensus_request()->CopyFrom(request);
    if (FLAGS_multi_raft_batch_size > 0
        && current_batch_->callbacks.size() >= FLAGS_multi_raft_batch_size) {
      data = PrepareNextBatchRequest();
    }
  }
  SendBatchRequest(data);
}

void MultiRaftHeartbeatBatcher::AddDataRequestToBatch(
    LWConsensusRequestPB* request, LWReplicateMsgsHolder msgs_holder,
    MultiRaftResponseCallback callback) {
  std::shared_ptr<MultiRaftConsensusData> data = nullptr;
  {
    std::lock_guard lock(mutex_);
    current_batch_->callbacks.push_back(std::move(callback));
    current_batch_->msgs_holders.push_back(std::move(msgs_holder));
    current_batch_->batch_req->mutable_consensus_request()->push_back_ref(request);
    current_batch_->has_data = true;
    if (in_flight_ < std::max<uint32_t>(FLAGS_multi_raft_batch_max_in_flight, 1)) {
      data = PrepareNextBatchRequest();
    } else {
      data_pending_ = true;
    }
  }
  SendBatchRequest(data);
}

void MultiRaftHeartbeatBatcher::PrepareAndSendBatchRequest() {
  std::shared_ptr<MultiRaftConsensusData> data;
  {
    std::lock_guard lock(mutex_);
    data = PrepareNextBatchRequest();
  }
  SendBatchRequest(data);
}

std::shared_ptr<MultiRaftHeartbeatBatcher::MultiRaftConsensusData>
    MultiRaftHeartbeatBatcher::PrepareNextBatchRequest() {
  if (!current_batch_ || current_batch_->callbacks.empty()) {
    return nullptr;
  }
  batch_sender_->Snooze();
  auto data = std::make_shared<MultiRaftConsensusData>();
  current_batch_.swap(data);
  data_pending_ = false;
  ++in_flight_;
  auto running_calls = ++*running_calls_;
  LOG_IF(DFATAL, running_calls <= 0) << "Wrong number or running calls: " << running_calls;
  return data;
}

void MultiRaftHeartbeatBatcher::SendBatchRequest(std::shared_ptr<MultiRaftConsensusData> data) {
  if (!data) {
    return;
  }
  // TODO(#16670): give consensus its own root trace.
  auto detach_token = dist_trace::DetachTraceContext();

  data->controller.Reset();
  data->controller.set_timeout(MonoDelta::FromMilliseconds(
      data->has_data ? FLAGS_consensus_rpc_timeout_ms
                     : FLAGS_consensus_rpc_timeout_ms * data->callbacks.size()));
  data->controller.set_invoke_callback_mode(rpc::InvokeCallbackMode::kThreadPoolHigh);
  consensus_proxy_->MultiRaftUpdateConsensusAsync(
      *data->batch_req, data->batch_res, &data->controller,
      [self = shared_from_this(), data] { self->BatchDone(data); });
}

void MultiRaftHeartbeatBatcher::BatchDone(std::shared_ptr<MultiRaftConsensusData> data) {
  auto status = data->controller.status();
  const auto num_requests = data->callbacks.size();
  if (status.ok() && data->batch_res->consensus_response().size() != num_requests) {
    status = STATUS_FORMAT(
        IllegalState, "Wrong number of responses in MultiRaftUpdateConsensus: $0, expected $1",
        data->batch_res->consensus_response().size(), num_requests);
  }
  std::shared_ptr<MultiRaftConsensusData> next;
  {
    std::lock_guard lock(mutex_);
    --in_flight_;
    if (data_pending_) {
      next = PrepareNextBatchRequest();
    }
  }
  --*running_calls_;
  auto response = status.ok() ? data->batch_res->consensus_response().begin()
                              : data->batch_res->consensus_response().end();
  for (auto& callback : data->callbacks) {
    if (status.ok()) {
      callback(status, &*response);
      ++response;
    } else {
      callback(status, nullptr);
    }
  }
  SendBatchRequest(next);
}

void MultiRaftHeartbeatBatcher::Shutdown() {
  decltype(current_batch_) batch;
  batch_sender_->Stop();
  {
    std::lock_guard lock(mutex_);
    batch.swap(current_batch_);
  }
  static const Status status = STATUS(Aborted, "MultiRaft shutdown");
  for (const auto& callback : batch->callbacks) {
    callback(status, nullptr);
  }
}

MultiRaftManager::MultiRaftManager(rpc::Messenger* messenger,
                                   rpc::ProxyCache* proxy_cache,
                                   CloudInfoPB local_peer_cloud_info_pb)
    : messenger_(messenger), proxy_cache_(proxy_cache),
      local_peer_cloud_info_pb_(std::move(local_peer_cloud_info_pb)) {}

MultiRaftHeartbeatBatcherPtr MultiRaftManager::AddOrGetBatcher(const RaftPeerPB& remote_peer_pb) {
  if (!FLAGS_enable_multi_raft_heartbeat_batcher &&
      !(FLAGS_multi_raft_batch_data_requests && FLAGS_enable_batched_tserver_rpcs)) {
    return nullptr;
  }

  auto hostport = HostPortFromPB(DesiredHostPort(remote_peer_pb, local_peer_cloud_info_pb_));
  std::lock_guard lock(mutex_);
  if (shutdown_) {
    LOG(DFATAL) << __func__ << " after shutdown";
    return nullptr;
  }
  MultiRaftHeartbeatBatcherPtr batcher;

  // After taking the lock, check if there is already a batcher
  // for the same remote host and return it.
  auto res = batchers_.find(hostport);
  if (res != batchers_.end() && (batcher = res->second.lock())) {
    return batcher;
  }
  batcher = std::make_shared<MultiRaftHeartbeatBatcher>(
      hostport, proxy_cache_, messenger_, &running_calls_);
  batchers_[hostport] = batcher;
  batcher->Start();
  return batcher;
}

MultiRaftManager::~MultiRaftManager() {
  LOG_IF(DFATAL, !shutdown_) << "Shutdown was not invoked";
}

void MultiRaftManager::StartShutdown() {
  std::vector<MultiRaftHeartbeatBatcherPtr> batchers;
  {
    std::lock_guard lock(mutex_);
    shutdown_ = true;
    batchers.reserve(batchers_.size());
    for (const auto& [hostport, weak_batcher] : batchers_) {
      auto batcher = weak_batcher.lock();
      if (batcher) {
        batchers.push_back(batcher);
      }
    }
    batchers_.clear();
  }
  for (const auto& batcher : batchers) {
    batcher->Shutdown();
  }
}

void MultiRaftManager::CompleteShutdown() {
  int expected = 0;
  while (!running_calls_.compare_exchange_weak(expected, std::numeric_limits<int>::min() / 2)) {
    std::this_thread::sleep_for(10ms);
    expected = 0;
  }
}

}  // namespace consensus
}  // namespace yb
