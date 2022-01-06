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

#include "yb/consensus/multi_raft_batcher.h"
#include <memory>

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/rpc/periodic.h"

#include "yb/util/flag_tags.h"

using namespace std::literals;
using namespace std::placeholders;

// NOTE: For tests set this value to ~10ms
DEFINE_int32(multi_raft_heartbeat_interval_ms, 10,
             "The heartbeat interval for batch Raft replication.");
TAG_FLAG(multi_raft_heartbeat_interval_ms, experimental);
TAG_FLAG(multi_raft_heartbeat_interval_ms, hidden);

// TODO: Add MultiRaftUpdateConsensus to metrics.yml when flag is turned on
DEFINE_bool(enable_multi_raft_heartbeat_batcher, false,
            "Whether to enable the batching of raft heartbeats.");
TAG_FLAG(enable_multi_raft_heartbeat_batcher, experimental);
TAG_FLAG(enable_multi_raft_heartbeat_batcher, hidden);

// NOTE: For tests set this value to 1
DEFINE_int32(multi_raft_batch_size, 1,
            "Maximum batch size for a multi-raft consensus payload.");
TAG_FLAG(multi_raft_batch_size, experimental);
TAG_FLAG(multi_raft_batch_size, hidden);

DECLARE_int32(consensus_rpc_timeout_ms);

namespace yb {
namespace consensus {

using rpc::PeriodicTimer;

struct MultiRaftHeartbeatBatcher::MultiRaftConsensusData {
  MultiRaftConsensusRequestPB batch_req;
  MultiRaftConsensusResponsePB batch_res;
  rpc::RpcController controller;
  std::vector<ResponseCallbackData> response_callback_data;
};

MultiRaftHeartbeatBatcher::MultiRaftHeartbeatBatcher(const yb::HostPort& hostport,
                                                     rpc::ProxyCache* proxy_cache,
                                                     rpc::Messenger* messenger):
    messenger_(messenger),
    consensus_proxy_(std::make_unique<ConsensusServiceProxy>(proxy_cache, hostport)),
    current_batch_(std::make_shared<MultiRaftConsensusData>()) {}

void MultiRaftHeartbeatBatcher::Start() {
  std::weak_ptr<MultiRaftHeartbeatBatcher> weak_peer = shared_from_this();
  batch_sender_ = PeriodicTimer::Create(
    messenger_,
    [weak_peer]() {
      if (auto peer = weak_peer.lock()) {
        peer->PrepareAndSendBatchRequest();
      }
    },
    MonoDelta::FromMilliseconds(FLAGS_multi_raft_heartbeat_interval_ms));
  batch_sender_->Start();
}

MultiRaftHeartbeatBatcher::~MultiRaftHeartbeatBatcher() = default;

void MultiRaftHeartbeatBatcher::AddRequestToBatch(ConsensusRequestPB* request,
                                                  ConsensusResponsePB* response,
                                                  HeartbeatResponseCallback callback) {
  std::shared_ptr<MultiRaftConsensusData> data = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_batch_->response_callback_data.push_back({
      response,
      std::move(callback)
    });
    // Add a ConsensusRequestPB to the batch
    current_batch_->batch_req.add_consensus_request()->Swap(request);
    if (FLAGS_multi_raft_batch_size > 0
        && current_batch_->response_callback_data.size() >= FLAGS_multi_raft_batch_size) {
      data = PrepareNextBatchRequest();
    }
  }
  if (data) {
    SendBatchRequest(data);
  }
}

void MultiRaftHeartbeatBatcher::PrepareAndSendBatchRequest() {
  std::shared_ptr<MultiRaftConsensusData> data;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    data = PrepareNextBatchRequest();
  }
  SendBatchRequest(data);
}

std::shared_ptr<MultiRaftHeartbeatBatcher::MultiRaftConsensusData>
    MultiRaftHeartbeatBatcher::PrepareNextBatchRequest() {
  if (current_batch_->batch_req.consensus_request_size() == 0) {
    return nullptr;
  }
  batch_sender_->Snooze();
  auto data = std::move(current_batch_);
  current_batch_ = std::make_shared<MultiRaftConsensusData>();
  return data;
}

void MultiRaftHeartbeatBatcher::SendBatchRequest(std::shared_ptr<MultiRaftConsensusData> data) {
  if (!data) {
    return;
  }

  data->controller.Reset();
  data->controller.set_timeout(MonoDelta::FromMilliseconds(
    FLAGS_consensus_rpc_timeout_ms * data->batch_req.consensus_request_size()));
  consensus_proxy_->MultiRaftUpdateConsensusAsync(
    data->batch_req, &data->batch_res, &data->controller,
    std::bind(&MultiRaftHeartbeatBatcher::MultiRaftUpdateHeartbeatResponseCallback,
              shared_from_this(), data));
}

void MultiRaftHeartbeatBatcher::MultiRaftUpdateHeartbeatResponseCallback(
    std::shared_ptr<MultiRaftConsensusData> data) {
  auto status = data->controller.status();
  for (int i = 0; i < data->batch_req.consensus_request_size(); i++) {
    auto callback_data = data->response_callback_data[i];
    if (status.ok()) {
      callback_data.resp->Swap(data->batch_res.mutable_consensus_response(i));
    }
    callback_data.callback(status);
  }
}

MultiRaftManager::MultiRaftManager(rpc::Messenger* messenger,
                                   rpc::ProxyCache* proxy_cache,
                                   CloudInfoPB local_peer_cloud_info_pb):
    messenger_(messenger), proxy_cache_(proxy_cache),
    local_peer_cloud_info_pb_(std::move(local_peer_cloud_info_pb)) {}

MultiRaftHeartbeatBatcherPtr MultiRaftManager::AddOrGetBatcher(const RaftPeerPB& remote_peer_pb) {
  if (!FLAGS_enable_multi_raft_heartbeat_batcher) {
    return nullptr;
  }

  auto hostport = HostPortFromPB(DesiredHostPort(remote_peer_pb, local_peer_cloud_info_pb_));
  std::lock_guard<std::mutex> lock(mutex_);
  MultiRaftHeartbeatBatcherPtr batcher;

  // After taking the lock, check if there is already a batcher
  // for the same remote host and return it.
  auto res = batchers_.find(hostport);
  if (res != batchers_.end() && (batcher = res->second.lock())) {
    return batcher;
  }
  batcher = std::make_shared<MultiRaftHeartbeatBatcher>(hostport, proxy_cache_, messenger_);
  batchers_[hostport] = batcher;
  batcher->Start();
  return batcher;
}

}  // namespace consensus
}  // namespace yb
