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

#pragma once

#include <memory>

#include "yb/common/common_net.pb.h"

#include "yb/consensus/consensus_fwd.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/net/net_util.h"

namespace yb {

namespace rpc {
class Messenger;
class PeriodicTimer;
}

namespace consensus {

using HeartbeatResponseCallback = std::function<void(const Status&)>;

// - MultiRaftHeartbeatBatcher is responsible for the batching of heartbeats
//   among peers that are communicating with remote peers at the same tserver
// - It is also responsible for periodically sending out these batched requests
//   and processing their responses
// - A heartbeat is added to a batch upon calling AddRequestToBatch and a batch is sent
//   out every FLAGS_multi_raft_heartbeat_interval_ms ms or once the batch size reaches
//   FLAGS_multi_raft_batch_size
// - To improve efficency multiple batches may be processed concurrently
//   but only a single batch is being built at any given time
class MultiRaftHeartbeatBatcher : public std::enable_shared_from_this<MultiRaftHeartbeatBatcher> {
 public:
  MultiRaftHeartbeatBatcher(const HostPort& hostport,
                            rpc::ProxyCache* proxy_cache,
                            rpc::Messenger* messenger,
                            std::atomic<int>* running_calls);

  ~MultiRaftHeartbeatBatcher();

  // Required to start a periodic timer to send out batches.
  void Start();

  // When called adds the request to a batch (request data will be swapped).
  // If the batch executes sucessfully then the response is populated and the callback is executed.
  // If the batch rpc call fails the response will NOT be populated and the callback will be
  // executed with an error status.
  void AddRequestToBatch(ConsensusRequestPB* request,
                         ConsensusResponsePB* response,
                         HeartbeatResponseCallback callback);

  void Shutdown();

 private:
  // Tracks all the metadata for a single batch request, including a list of all
  // ResponseCallbackData registered by each local peer with this batch in AddRequestToBatch().
  struct MultiRaftConsensusData;

  void PrepareAndSendBatchRequest();

  // This method will return a nullptr if the current batch is empty.
  std::shared_ptr<MultiRaftConsensusData> PrepareNextBatchRequest() REQUIRES(mutex_);

  // If data is null then we will not send a batch request.
  void SendBatchRequest(std::shared_ptr<MultiRaftConsensusData> data);

  void MultiRaftUpdateHeartbeatResponseCallback(std::shared_ptr<MultiRaftConsensusData> data);

  rpc::Messenger* messenger_;

  ConsensusServiceProxyPtr consensus_proxy_;

  std::shared_ptr<rpc::PeriodicTimer> batch_sender_;

  std::mutex mutex_;

  std::shared_ptr<MultiRaftConsensusData> current_batch_ GUARDED_BY(mutex_);

  std::atomic<int>* running_calls_;
};

// MultiRaftManager is responsible for managing all MultiRaftHeartbeatBatchers
// for a given tserver (utilizes a mapping between a hostport and the corresponding batcher).
// MultiRaftManager allows multiple peers to share the same batcher
// if they are connected to the same remote host.
class MultiRaftManager: public std::enable_shared_from_this<MultiRaftManager> {
 public:
  MultiRaftManager(rpc::Messenger* messenger,
                   rpc::ProxyCache* proxy_cache,
                   CloudInfoPB local_peer_cloud_info_pb);

  ~MultiRaftManager();

  // Add a batcher with the given hostport (if one does not already exist)
  // and returns the newly created batcher.
  MultiRaftHeartbeatBatcherPtr AddOrGetBatcher(const RaftPeerPB& remote_peer_pb);

  void StartShutdown();
  void CompleteShutdown();

 private:
  rpc::Messenger* messenger_;

  rpc::ProxyCache* proxy_cache_;

  CloudInfoPB local_peer_cloud_info_pb_;

  std::mutex mutex_;

  // Uses a weak_ptr value in the map to allow for deallocation of unneeded batchers
  // since each consensus peer will own a shared_ptr to the batcher (performance optimization).
  // MultiRaftBatchers will be shared for the same remote peer info.
  std::unordered_map<HostPort, std::weak_ptr<MultiRaftHeartbeatBatcher>,
                     HostPortHash> batchers_ GUARDED_BY(mutex_);

  bool shutdown_ = false;
  std::atomic<int> running_calls_{0};
};

}   // namespace consensus
}   // namespace yb
