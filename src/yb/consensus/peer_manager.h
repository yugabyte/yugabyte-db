// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
#pragma once

#include <string>
#include <unordered_map>

#include "yb/util/flags.h"

#include "yb/consensus/log_fwd.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/multi_raft_batcher.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/shared_lock.h"

namespace yb {

class MemTracker;
class ThreadPoolToken;

namespace consensus {

class Consensus;
class Peer;
class PeerMessageQueue;
class PeerProxyFactory;
class RaftConfigPB;

// Manages the set of local and remote peers that pull data from the queue into the local log/remote
// machines.  Methods are virtual to ease mocking.
class PeerManager {
 public:
  // All of the raw pointer arguments are not owned by the PeerManager and must live at least as
  // long as the PeerManager.
  PeerManager(const std::string tablet_id,
              const std::string local_uuid,
              PeerProxyFactory* peer_proxy_factory,
              PeerMessageQueue* queue,
              ThreadPoolToken* raft_pool_token,
              MultiRaftManager* multi_raft_manager);

  virtual ~PeerManager();

  virtual void SetConsensus(Consensus* consensus) {consensus_ = consensus; }

  // Updates 'peers_' according to the new configuration config.
  virtual void UpdateRaftConfig(const RaftConfigPB& config);

  // Signals all peers of the current configuration that there is a new request pending.
  virtual void SignalRequest(RequestTriggerMode trigger_mode);

  // Closes all peers.
  virtual void Close();

  // Closes connections to those peers that are not in config.
  virtual void ClosePeersNotInConfig(const RaftConfigPB& config);

  virtual void DumpToHtml(std::ostream& out) const;

 private:
  std::string LogPrefix() const;

  typedef std::unordered_map<std::string, std::shared_ptr<Peer>> PeersMap;
  const std::string tablet_id_;
  const std::string local_uuid_;
  PeerProxyFactory* peer_proxy_factory_;
  PeerMessageQueue* queue_;
  ThreadPoolToken* raft_pool_token_;
  MultiRaftManager* multi_raft_manager_;
  PeersMap peers_;
  Consensus* consensus_ = nullptr;
  mutable simple_spinlock lock_;

  DISALLOW_COPY_AND_ASSIGN(PeerManager);
};

} // namespace consensus
} // namespace yb
