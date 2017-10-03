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
#ifndef KUDU_MASTER_TS_DESCRIPTOR_H
#define KUDU_MASTER_TS_DESCRIPTOR_H

#include <memory>
#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"

namespace kudu {

class NodeInstancePB;
class Sockaddr;

namespace consensus {
class ConsensusServiceProxy;
}

namespace rpc {
class Messenger;
}

namespace tserver {
class TabletServerAdminServiceProxy;
}

namespace master {

class TSRegistrationPB;

// Master-side view of a single tablet server.
//
// Tracks the last heartbeat, status, instance identifier, etc.
// This class is thread-safe.
class TSDescriptor {
 public:
  static Status RegisterNew(const NodeInstancePB& instance,
                            const TSRegistrationPB& registration,
                            gscoped_ptr<TSDescriptor>* desc);

  virtual ~TSDescriptor();

  // Set the last-heartbeat time to now.
  void UpdateHeartbeatTime();

  // Return the amount of time since the last heartbeat received
  // from this TS.
  MonoDelta TimeSinceHeartbeat() const;

  // Register this tablet server.
  Status Register(const NodeInstancePB& instance,
                  const TSRegistrationPB& registration);

  const std::string &permanent_uuid() const { return permanent_uuid_; }
  int64_t latest_seqno() const;

  bool has_tablet_report() const;
  void set_has_tablet_report(bool has_report);

  // Copy the current registration info into the given PB object.
  // A safe copy is returned because the internal Registration object
  // may be mutated at any point if the tablet server re-registers.
  void GetRegistration(TSRegistrationPB* reg) const;

  void GetNodeInstancePB(NodeInstancePB* instance_pb) const;

  // Return an RPC proxy to the tablet server admin service.
  Status GetTSAdminProxy(const std::shared_ptr<rpc::Messenger>& messenger,
                         std::shared_ptr<tserver::TabletServerAdminServiceProxy>* proxy);

  // Return an RPC proxy to the consensus service.
  Status GetConsensusProxy(const std::shared_ptr<rpc::Messenger>& messenger,
                           std::shared_ptr<consensus::ConsensusServiceProxy>* proxy);

  // Increment the accounting of the number of replicas recently created on this
  // server. This value will automatically decay over time.
  void IncrementRecentReplicaCreations();

  // Return the number of replicas which have recently been created on this
  // TS. This number is incremented when replicas are placed on the TS, and
  // then decayed over time. This method is not 'const' because each call
  // actually performs the time-based decay.
  double RecentReplicaCreations();

  // Set the number of live replicas (i.e. running or bootstrapping).
  void set_num_live_replicas(int n) {
    DCHECK_GE(n, 0);
    lock_guard<simple_spinlock> l(&lock_);
    num_live_replicas_ = n;
  }

  // Return the number of live replicas (i.e running or bootstrapping).
  int num_live_replicas() const {
    lock_guard<simple_spinlock> l(&lock_);
    return num_live_replicas_;
  }

 private:
  FRIEND_TEST(TestTSDescriptor, TestReplicaCreationsDecay);

  explicit TSDescriptor(std::string perm_id);

  // Uses DNS to resolve registered hosts to a single Sockaddr.
  Status ResolveSockaddr(Sockaddr* addr) const;

  void DecayRecentReplicaCreationsUnlocked();

  mutable simple_spinlock lock_;

  const std::string permanent_uuid_;
  int64_t latest_seqno_;

  // The last time a heartbeat was received for this node.
  MonoTime last_heartbeat_;

  // Set to true once this instance has reported all of its tablets.
  bool has_tablet_report_;

  // The number of times this tablet server has recently been selected to create a
  // tablet replica. This value decays back to 0 over time.
  double recent_replica_creations_;
  MonoTime last_replica_creations_decay_;

  // The number of live replicas on this host, from the last heartbeat.
  int num_live_replicas_;

  gscoped_ptr<TSRegistrationPB> registration_;

  std::shared_ptr<tserver::TabletServerAdminServiceProxy> ts_admin_proxy_;
  std::shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy_;

  DISALLOW_COPY_AND_ASSIGN(TSDescriptor);
};

} // namespace master
} // namespace kudu
#endif /* KUDU_MASTER_TS_DESCRIPTOR_H */
