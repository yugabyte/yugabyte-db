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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/client/meta_cache.h"

#include <shared_mutex>
#include <mutex>

#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/client/table.h"

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/flag_tags.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/net/net_util.h"
#include "yb/util/shared_lock.h"

using std::string;
using std::map;
using std::shared_ptr;
using strings::Substitute;
using namespace std::literals;  // NOLINT

DEFINE_int32(max_concurrent_master_lookups, 500,
             "Maximum number of concurrent tablet location lookups from YB client to master");

DEFINE_test_flag(bool, verify_all_replicas_alive, false,
                 "If set, when a RemoteTablet object is destroyed, we will verify that all its "
                 "replicas are not marked as failed");

DEFINE_int32(retry_failed_replica_ms, 60 * 1000,
             "Time in milliseconds to wait for before retrying a failed replica");

METRIC_DEFINE_histogram(
  server, dns_resolve_latency_during_init_proxy,
  "yb.client.MetaCache.InitProxy DNS Resolve",
  yb::MetricUnit::kMicroseconds,
  "Microseconds spent resolving DNS requests during MetaCache::InitProxy",
  60000000LU, 2);

namespace yb {

using consensus::RaftPeerPB;
using master::GetTableLocationsRequestPB;
using master::GetTableLocationsResponsePB;
using master::MasterServiceProxy;
using master::TabletLocationsPB;
using master::TabletLocationsPB_ReplicaPB;
using master::TSInfoPB;
using rpc::Messenger;
using rpc::Rpc;
using tablet::RaftGroupStatePB;
using tserver::LocalTabletServer;
using tserver::TabletServerServiceProxy;

namespace client {

namespace internal {

namespace {

// We join tablet partitions to groups, so tablet state info in one group requested with single
// RPC call to master.
// kPartitionGroupSize defines size of this group.
#ifdef NDEBUG
const size_t kPartitionGroupSize = 64;
#else
const size_t kPartitionGroupSize = 4;
#endif

} // namespace

////////////////////////////////////////////////////////////

RemoteTabletServer::RemoteTabletServer(const master::TSInfoPB& pb)
    : uuid_(pb.permanent_uuid()) {
  Update(pb);
}

RemoteTabletServer::RemoteTabletServer(const string& uuid,
                                       const shared_ptr<TabletServerServiceProxy>& proxy,
                                       const LocalTabletServer* local_tserver)
    : uuid_(uuid),
      proxy_(proxy),
      local_tserver_(local_tserver) {
  LOG_IF(DFATAL, proxy && !IsLocal()) << "Local tserver has non-local proxy";
}

Status RemoteTabletServer::InitProxy(YBClient* client) {
  {
    SharedLock<rw_spinlock> lock(mutex_);

    if (proxy_) {
      // Already have a proxy created.
      return Status::OK();
    }
  }

  std::lock_guard<rw_spinlock> lock(mutex_);

  if (proxy_) {
    // Already have a proxy created.
    return Status::OK();
  }

  if (!dns_resolve_histogram_) {
    auto metric_entity = client->metric_entity();
    if (metric_entity) {
      dns_resolve_histogram_ = METRIC_dns_resolve_latency_during_init_proxy.Instantiate(
          metric_entity);
    }
  }

  // TODO: if the TS advertises multiple host/ports, pick the right one
  // based on some kind of policy. For now just use the first always.
  auto hostport = HostPortFromPB(DesiredHostPort(
      public_rpc_hostports_, private_rpc_hostports_, cloud_info_pb_,
      client->data_->cloud_info_pb_));
  CHECK(!hostport.host().empty());
  ScopedDnsTracker dns_tracker(dns_resolve_histogram_.get());
  proxy_.reset(new TabletServerServiceProxy(client->data_->proxy_cache_.get(), hostport));
  proxy_endpoint_ = hostport;

  return Status::OK();
}

void RemoteTabletServer::Update(const master::TSInfoPB& pb) {
  CHECK_EQ(pb.permanent_uuid(), uuid_);

  std::lock_guard<rw_spinlock> lock(mutex_);
  private_rpc_hostports_ = pb.private_rpc_addresses();
  public_rpc_hostports_ = pb.broadcast_addresses();
  cloud_info_pb_ = pb.cloud_info();
  capabilities_.assign(pb.capabilities().begin(), pb.capabilities().end());
  std::sort(capabilities_.begin(), capabilities_.end());
}

bool RemoteTabletServer::IsLocal() const {
  return local_tserver_ != nullptr;
}

const std::string& RemoteTabletServer::permanent_uuid() const {
  return uuid_;
}

const CloudInfoPB& RemoteTabletServer::cloud_info() const {
  return cloud_info_pb_;
}

const google::protobuf::RepeatedPtrField<HostPortPB>&
    RemoteTabletServer::public_rpc_hostports() const {
  return public_rpc_hostports_;
}

const google::protobuf::RepeatedPtrField<HostPortPB>&
    RemoteTabletServer::private_rpc_hostports() const {
  return private_rpc_hostports_;
}

shared_ptr<TabletServerServiceProxy> RemoteTabletServer::proxy() const {
  SharedLock<rw_spinlock> lock(mutex_);
  return proxy_;
}

::yb::HostPort RemoteTabletServer::ProxyEndpoint() const {
  std::shared_lock<rw_spinlock> lock(mutex_);
  return proxy_endpoint_;
}

string RemoteTabletServer::ToString() const {
  string ret = "{ uuid: " + uuid_;
  SharedLock<rw_spinlock> lock(mutex_);
  if (!private_rpc_hostports_.empty()) {
    ret += Format(" private: $0", private_rpc_hostports_);
  }
  if (!public_rpc_hostports_.empty()) {
    ret += Format(" public: $0", public_rpc_hostports_);
  }
  ret += Format(" cloud_info: $0", cloud_info_pb_);
  return ret;
}

bool RemoteTabletServer::HasHostFrom(const std::unordered_set<std::string>& hosts) const {
  SharedLock<rw_spinlock> lock(mutex_);
  for (const auto& hp : private_rpc_hostports_) {
    if (hosts.count(hp.host())) {
      return true;
    }
  }
  for (const auto& hp : public_rpc_hostports_) {
    if (hosts.count(hp.host())) {
      return true;
    }
  }
  return false;
}

bool RemoteTabletServer::HasCapability(CapabilityId capability) const {
  SharedLock<rw_spinlock> lock(mutex_);
  return std::binary_search(capabilities_.begin(), capabilities_.end(), capability);
}

////////////////////////////////////////////////////////////

RemoteTablet::~RemoteTablet() {
  if (PREDICT_FALSE(FLAGS_verify_all_replicas_alive)) {
    // Let's verify that none of the replicas are marked as failed. The test should always wait
    // enough time so that the lookup cache can be refreshed after force_lookup_cache_refresh_secs.
    for (const auto& replica : replicas_) {
      if (replica.Failed()) {
        LOG_WITH_PREFIX(FATAL) << "Remote tablet server " << replica.ts->ToString()
                               << " with role " << consensus::RaftPeerPB::Role_Name(replica.role)
                               << " is marked as failed";
      }
    }
  }
}

void RemoteTablet::Refresh(
    const TabletServerMap& tservers,
    const google::protobuf::RepeatedPtrField<TabletLocationsPB_ReplicaPB>& replicas) {
  // Adopt the data from the successful response.
  std::lock_guard<rw_spinlock> lock(mutex_);
  replicas_.clear();
  for (const TabletLocationsPB_ReplicaPB& r : replicas) {
    auto it = tservers.find(r.ts_info().permanent_uuid());
    CHECK(it != tservers.end());
    replicas_.emplace_back(it->second.get(), r.role());
  }
  stale_ = false;
  refresh_time_.store(MonoTime::Now(), std::memory_order_release);
}

void RemoteTablet::MarkStale() {
  std::lock_guard<rw_spinlock> lock(mutex_);
  stale_ = true;
}

bool RemoteTablet::stale() const {
  SharedLock<rw_spinlock> lock(mutex_);
  return stale_;
}

void RemoteTablet::MarkAsSplit() {
  std::lock_guard<rw_spinlock> lock(mutex_);
  is_split_ = true;
}

bool RemoteTablet::is_split() const {
  SharedLock<rw_spinlock> lock(mutex_);
  return is_split_;
}

bool RemoteTablet::MarkReplicaFailed(RemoteTabletServer *ts, const Status& status) {
  std::lock_guard<rw_spinlock> lock(mutex_);
  VLOG_WITH_PREFIX(2) << "Current remote replicas in meta cache: "
                      << ReplicasAsStringUnlocked() << ". Replica " << ts->ToString()
                      << " has failed: " << status.ToString();
  for (RemoteReplica& rep : replicas_) {
    if (rep.ts == ts) {
      rep.MarkFailed();
      return true;
    }
  }
  return false;
}

int RemoteTablet::GetNumFailedReplicas() const {
  int failed = 0;
  SharedLock<rw_spinlock> lock(mutex_);
  for (const RemoteReplica& rep : replicas_) {
    if (rep.Failed()) {
      failed++;
    }
  }
  return failed;
}

RemoteTabletServer* RemoteTablet::LeaderTServer() const {
  SharedLock<rw_spinlock> lock(mutex_);
  for (const RemoteReplica& replica : replicas_) {
    if (!replica.Failed() && replica.role == RaftPeerPB::LEADER) {
      return replica.ts;
    }
  }
  return nullptr;
}

bool RemoteTablet::HasLeader() const {
  return LeaderTServer() != nullptr;
}

void RemoteTablet::GetRemoteTabletServers(
    std::vector<RemoteTabletServer*>* servers, IncludeFailedReplicas include_failed_replicas) {
  DCHECK(servers->empty());
  struct ReplicaUpdate {
    RemoteReplica* replica;
    tablet::RaftGroupStatePB new_state;
    bool clear_failed;
  };
  std::vector<ReplicaUpdate> replica_updates;
  {
    SharedLock<rw_spinlock> lock(mutex_);
    for (RemoteReplica& replica : replicas_) {
      if (replica.Failed()) {
        if (include_failed_replicas) {
          servers->push_back(replica.ts);
          continue;
        }
        ReplicaUpdate replica_update = {&replica, RaftGroupStatePB::UNKNOWN, false};
        VLOG_WITH_PREFIX(4)
            << "Replica " << replica.ts->ToString()
            << " failed, state: " << RaftGroupStatePB_Name(replica.state)
            << ", is local: " << replica.ts->IsLocal()
            << ", time since failure: " << (MonoTime::Now() - replica.last_failed_time);
        switch (replica.state) {
          case RaftGroupStatePB::UNKNOWN: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::NOT_STARTED: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::BOOTSTRAPPING: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::RUNNING:
            // These are non-terminal states that may retry. Check and update failed local replica's
            // current state. For remote replica, just wait for some time before retrying.
            if (replica.ts->IsLocal()) {
              tserver::GetTabletStatusRequestPB req;
              tserver::GetTabletStatusResponsePB resp;
              req.set_tablet_id(tablet_id_);
              const Status status =
                  CHECK_NOTNULL(replica.ts->local_tserver())->GetTabletStatus(&req, &resp);
              if (!status.ok() || resp.has_error()) {
                LOG_WITH_PREFIX(ERROR)
                    << "Received error from GetTabletStatus: "
                    << (!status.ok() ? status : StatusFromPB(resp.error().status()));
                continue;
              }

              DCHECK_EQ(resp.tablet_status().tablet_id(), tablet_id_);
              VLOG_WITH_PREFIX(3) << "GetTabletStatus returned status: "
                                  << tablet::RaftGroupStatePB_Name(resp.tablet_status().state())
                                  << " for replica " << replica.ts->ToString();
              replica_update.new_state = resp.tablet_status().state();
              if (replica_update.new_state != tablet::RaftGroupStatePB::RUNNING) {
                if (replica_update.new_state != replica.state) {
                  // Cannot update replica here directly because holding only shared lock on mutex.
                  replica_updates.push_back(replica_update); // Update only state
                }
                continue;
              }
              if (!replica.ts->local_tserver()->LeaderAndReady(
                      tablet_id_, /* allow_stale */ true)) {
                // Should continue here because otherwise failed state will be cleared.
                continue;
              }
            } else if ((MonoTime::Now() - replica.last_failed_time) <
                       FLAGS_retry_failed_replica_ms * 1ms) {
              continue;
            }
            break;
          case RaftGroupStatePB::FAILED: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::QUIESCING: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::SHUTDOWN:
            // These are terminal states, so we won't retry.
            continue;
        }

        VLOG_WITH_PREFIX(3) << "Changing state of replica " << replica.ts->ToString()
                            << " from failed to not failed";
        replica_update.clear_failed = true;
        // Cannot update replica here directly because holding only shared lock on mutex.
        replica_updates.push_back(replica_update);
      }
      servers->push_back(replica.ts);
    }
  }
  if (!replica_updates.empty()) {
    std::lock_guard<rw_spinlock> lock(mutex_);
    for (const auto& update : replica_updates) {
      if (update.new_state != RaftGroupStatePB::UNKNOWN) {
        update.replica->state = update.new_state;
      }
      if (update.clear_failed) {
        update.replica->ClearFailed();
      }
    }
  }
}

bool RemoteTablet::MarkTServerAsLeader(const RemoteTabletServer* server) {
  bool found = false;
  std::lock_guard<rw_spinlock> lock(mutex_);
  for (RemoteReplica& replica : replicas_) {
    if (replica.ts == server) {
      replica.role = RaftPeerPB::LEADER;
      found = true;
    } else if (replica.role == RaftPeerPB::LEADER) {
      replica.role = RaftPeerPB::FOLLOWER;
    }
  }
  VLOG_WITH_PREFIX(3) << "Latest replicas: " << ReplicasAsStringUnlocked();
  VLOG_IF_WITH_PREFIX(3, !found) << "Specified server not found: " << server->ToString()
                                 << ". Replicas: " << ReplicasAsStringUnlocked();
  return found;
}

void RemoteTablet::MarkTServerAsFollower(const RemoteTabletServer* server) {
  bool found = false;
  std::lock_guard<rw_spinlock> lock(mutex_);
  for (RemoteReplica& replica : replicas_) {
    if (replica.ts == server) {
      replica.role = RaftPeerPB::FOLLOWER;
      found = true;
    }
  }
  VLOG_WITH_PREFIX(3) << "Latest replicas: " << ReplicasAsStringUnlocked();
  DCHECK(found) << "Tablet " << tablet_id_ << ": Specified server not found: "
                << server->ToString() << ". Replicas: " << ReplicasAsStringUnlocked();
}

std::string RemoteTablet::ReplicasAsString() const {
  SharedLock<rw_spinlock> lock(mutex_);
  return ReplicasAsStringUnlocked();
}

std::string RemoteTablet::ReplicasAsStringUnlocked() const {
  DCHECK(mutex_.is_locked());
  string replicas_str;
  for (const RemoteReplica& rep : replicas_) {
    if (!replicas_str.empty()) replicas_str += ", ";
    replicas_str += rep.ToString();
  }
  return replicas_str;
}

std::string RemoteTablet::ToString() const {
  return Format("{ tablet_id: $0 }", tablet_id_);
}

////////////////////////////////////////////////////////////

MetaCache::MetaCache(YBClient* client)
  : client_(client),
    master_lookup_sem_(FLAGS_max_concurrent_master_lookups) {
}

MetaCache::~MetaCache() {
  Shutdown();
}

void MetaCache::Shutdown() {
  rpcs_.Shutdown();
}

void MetaCache::SetLocalTabletServer(const string& permanent_uuid,
                                     const shared_ptr<TabletServerServiceProxy>& proxy,
                                     const LocalTabletServer* local_tserver) {
  const auto entry = ts_cache_.emplace(permanent_uuid,
                                       std::make_unique<RemoteTabletServer>(permanent_uuid,
                                                                            proxy,
                                                                            local_tserver));
  CHECK(entry.second);
  local_tserver_ = entry.first->second.get();
}

void MetaCache::UpdateTabletServerUnlocked(const master::TSInfoPB& pb) {
  const std::string& permanent_uuid = pb.permanent_uuid();
  auto it = ts_cache_.find(permanent_uuid);
  if (it != ts_cache_.end()) {
    it->second->Update(pb);
    return;
  }

  VLOG(1) << "Client caching new TabletServer " << permanent_uuid;
  CHECK(ts_cache_.emplace(permanent_uuid, std::make_unique<RemoteTabletServer>(pb)).second);
}

// A (table, partition_key) --> tablet lookup. May be in-flight to a master, or
// may be handled locally.
//
// Keeps a reference on the owning metacache while alive.
class LookupRpc : public Rpc {
 public:
  LookupRpc(const scoped_refptr<MetaCache>& meta_cache,
            CoarseTimePoint deadline,
            Messenger* messenger,
            rpc::ProxyCache* proxy_cache);

  virtual ~LookupRpc();

  void SendRpc() override;

  MetaCache* meta_cache() { return meta_cache_.get(); }
  YBClient* client() const { return meta_cache_->client_; }

  void ResetMasterLeaderAndRetry();

  virtual void Notify(const Status& status, const RemoteTabletPtr& result = nullptr) = 0;

  std::shared_ptr<MasterServiceProxy> master_proxy() const {
    return client()->data_->master_proxy();
  }

  template <class Response>
  void DoFinished(const Status& status, const Response& resp,
                  const std::string* partition_group_start);

  std::string LogPrefix() const {
    return yb::ToString(this) + ": ";
  }

 private:
  virtual void DoSendRpc() = 0;

  void NewLeaderMasterDeterminedCb(const Status& status);

  // Pointer back to the tablet cache. Populated with location information
  // if the lookup finishes successfully.
  //
  // When the RPC is destroyed, a master lookup permit is returned to the
  // cache if one was acquired in the first place.
  scoped_refptr<MetaCache> meta_cache_;

  // Whether this lookup has acquired a master lookup permit.
  bool has_permit_ = false;

  rpc::Rpcs::Handle retained_self_;
};

LookupRpc::LookupRpc(const scoped_refptr<MetaCache>& meta_cache,
                     CoarseTimePoint deadline,
                     Messenger* messenger,
                     rpc::ProxyCache* proxy_cache)
    : Rpc(deadline, messenger, proxy_cache),
      meta_cache_(meta_cache),
      retained_self_(meta_cache_->rpcs_.InvalidHandle()) {
  DCHECK(deadline != CoarseTimePoint());
}

LookupRpc::~LookupRpc() {
  if (has_permit_) {
    meta_cache_->ReleaseMasterLookupPermit();
  }
}

void LookupRpc::SendRpc() {
  meta_cache_->rpcs_.Register(shared_from_this(), &retained_self_);

  if (!has_permit_) {
    has_permit_ = meta_cache_->AcquireMasterLookupPermit();
  }
  if (!has_permit_) {
    // Couldn't get a permit, try again in a little while.
    ScheduleRetry(STATUS(TryAgain, "Client has too many outstanding requests to the master"));
    return;
  }

  // See YBClient::Data::SyncLeaderMasterRpc().
  auto now = CoarseMonoClock::Now();
  if (retrier().deadline() < now) {
    Finished(STATUS(TimedOut, "timed out after deadline expired"));
    return;
  }
  mutable_retrier()->PrepareController(MonoDelta());

  DoSendRpc();
}

void LookupRpc::ResetMasterLeaderAndRetry() {
  client()->data_->SetMasterServerProxyAsync(
      retrier().deadline(),
      false /* skip_resolution */,
      true /* wait for leader election */,
      Bind(&LookupRpc::NewLeaderMasterDeterminedCb,
           Unretained(this)));
}

void LookupRpc::NewLeaderMasterDeterminedCb(const Status& status) {
  if (status.ok()) {
    mutable_retrier()->mutable_controller()->Reset();
    SendRpc();
  } else {
    LOG_WITH_PREFIX(WARNING) << "Failed to determine new Master: " << status;
    ScheduleRetry(status);
  }
}

template <class Response>
void LookupRpc::DoFinished(
    const Status& status, const Response& resp, const std::string* partition_group_start) {
  if (status.ok() && resp.has_error()) {
    LOG_WITH_PREFIX(INFO)
        << "Failed, got resp error " << master::MasterErrorPB::Code_Name(resp.error().code());
  } else if (!status.ok()) {
    LOG_WITH_PREFIX(INFO) << "Failed: " << status;
  }

  // Prefer early failures over controller failures.
  Status new_status = status;
  if (new_status.ok() &&
      mutable_retrier()->HandleResponse(this, &new_status, rpc::RetryWhenBusy::kFalse)) {
    return;
  }

  // Prefer controller failures over response failures.
  if (new_status.ok() && resp.has_error()) {
    new_status = StatusFromPB(resp.error().status());
    if (resp.error().code() == master::MasterErrorPB::NOT_THE_LEADER ||
        resp.error().code() == master::MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
      if (client()->IsMultiMaster()) {
        YB_LOG_EVERY_N_SECS(WARNING, 1) << "Leader Master has changed, re-trying...";
        ResetMasterLeaderAndRetry();
      } else {
        ScheduleRetry(new_status);
      }
      return;
    }
  }

  if (new_status.IsTimedOut()) {
    if (CoarseMonoClock::Now() < retrier().deadline()) {
      YB_LOG_EVERY_N_SECS(WARNING, 1) << "Leader Master timed out, re-trying...";
      ResetMasterLeaderAndRetry();
      return;
    } else {
      // Operation deadline expired during this latest RPC.
      new_status = new_status.CloneAndPrepend("timed out after deadline expired");
    }
  }

  if (new_status.IsNetworkError() || new_status.IsRemoteError()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1) << "Encountered a error from the Master: "
         << new_status << ", retrying...";
    ResetMasterLeaderAndRetry();
    return;
  }

  // Prefer response failures over no tablets found.
  if (new_status.ok() && resp.tablet_locations_size() == 0) {
    new_status = STATUS(NotFound, "No such tablet found");
  }

  auto retained_self = meta_cache_->rpcs_.Unregister(&retained_self_);

  if (new_status.ok()) {
    Notify(Status::OK(),
           meta_cache_->ProcessTabletLocations(resp.tablet_locations(), partition_group_start));
  } else {
    LOG_WITH_PREFIX(WARNING) << new_status;
    new_status = new_status.CloneAndPrepend(Substitute("$0 failed", ToString()));
    Notify(new_status);
  }
}

RemoteTabletPtr MetaCache::ProcessTabletLocations(
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
    const std::string* partition_group_start) {
  VLOG(2) << "Processing master response " << ToString(locations);

  RemoteTabletPtr result;
  bool first = true;
  std::vector<std::pair<LookupTabletCallback, internal::RemoteTabletPtr>> to_notify;

  {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    for (const TabletLocationsPB& loc : locations) {
      for (const std::string& table_id : loc.table_ids()) {
        auto& table_data = tables_[table_id];
        auto& tablets_by_key = table_data.tablets_by_partition;
        // First, update the tserver cache, needed for the Refresh calls below.
        for (const TabletLocationsPB_ReplicaPB& r : loc.replicas()) {
          UpdateTabletServerUnlocked(r.ts_info());
        }

        // Next, update the tablet caches.
        const std::string& tablet_id = loc.tablet_id();
        RemoteTabletPtr remote = FindPtrOrNull(tablets_by_id_, tablet_id);
        if (remote) {
          // Partition should not have changed.
          DCHECK_EQ(loc.partition().partition_key_start(),
                    remote->partition().partition_key_start());
          DCHECK_EQ(loc.partition().partition_key_end(),
                    remote->partition().partition_key_end());

          // For colocated tables, RemoteTablet already exists because it was processed
          // in a previous iteration of the for loop (for loc.table_ids()).
          // We need to add this tablet to the current table's tablets_by_key map.
          tablets_by_key[remote->partition().partition_key_start()] = remote;

          VLOG(3) << "Refreshing tablet " << tablet_id << ": " << loc.ShortDebugString();
        } else {
          VLOG(3) << "Caching tablet " << tablet_id << ": " << loc.ShortDebugString();

          Partition partition;
          Partition::FromPB(loc.partition(), &partition);
          remote = new RemoteTablet(
              tablet_id, partition, loc.split_depth());

          CHECK(tablets_by_id_.emplace(tablet_id, remote).second);
          // TODO(tsplit): cover this functionality with test.
          auto emplace_result = tablets_by_key.emplace(partition.partition_key_start(), remote);
          if (!emplace_result.second) {
            if (emplace_result.first->second->split_depth() < remote->split_depth()) {
              // Only replace with tablet of higher split_depth.
              emplace_result.first->second = remote;
            } else {
              // If split_depth is the same - it should be the same tablet.
              CHECK(emplace_result.first->second->split_depth() != remote->split_depth() ||
                    emplace_result.first->second->tablet_id() == tablet_id);
            }
          }
        }
        remote->Refresh(ts_cache_, loc.replicas());

        if (first) {
          result = remote;
          first = false;
        }

        if (partition_group_start) {
          auto lookup_by_group_iter =
              table_data.tablet_lookups_by_group.find(*partition_group_start);
          if (lookup_by_group_iter != table_data.tablet_lookups_by_group.end()) {
            auto& lookups_by_partition_key = lookup_by_group_iter->second;
            auto lookups_iter =
                lookups_by_partition_key.find(loc.partition().partition_key_start());
            if (lookups_iter != lookups_by_partition_key.end()) {
              for (auto& lookup : lookups_iter->second) {
                to_notify.emplace_back(std::move(lookup.callback), remote);
              }
              lookups_by_partition_key.erase(lookups_iter);
            }
            if (lookups_by_partition_key.empty()) {
              table_data.tablet_lookups_by_group.erase(lookup_by_group_iter);
            }
          }
        }
      }
    }
  }

  for (const auto& callback_and_remote_tablet : to_notify) {
    callback_and_remote_tablet.first(callback_and_remote_tablet.second);
  }

  CHECK_NOTNULL(result.get());
  return result;
}

void MetaCache::InvalidateTableCache(const TableId& table_id) {
  std::lock_guard<decltype(mutex_)> lock(mutex_);
  tables_.erase(table_id);
  // TODO(tsplit): Instead of silently removing callbacks for lookups in progress - handle
  // this case appropriately.
  // TODO(tsplit): Optimize to retry only necessary lookups inside ProcessTabletLocations,
  // detect which need to be retried by GetTableLocationsResponsePB.partition_version.
}

void MetaCache::LookupFailed(
    const YBTable* table, const std::string& partition_group_start, const Status& status) {
  VLOG(1) << "Lookup for table " << table->id() << " and partition "
          << Slice(partition_group_start).ToDebugHexString() << ", failed with: " << status;

  std::vector<LookupTabletCallback> to_notify;
  CoarseTimePoint max_deadline;
  {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    auto it = tables_.find(table->id());
    if (it == tables_.end()) {
      return;
    }

    auto gi = it->second.tablet_lookups_by_group.find(partition_group_start);
    if (gi == it->second.tablet_lookups_by_group.end()) {
      return;
    }
    auto& lookups = gi->second;

    if (!status.IsTimedOut()) {
      for (const auto& pair : lookups) {
        for (auto& data : pair.second) {
          to_notify.push_back(std::move(data.callback));
        }
      }
      it->second.tablet_lookups_by_group.erase(gi);
    } else {
      auto now = CoarseMonoClock::Now();
      for (auto j = lookups.begin(); j != lookups.end();) {
        auto w = j->second.begin();
        for (auto i = j->second.begin(); i != j->second.end(); ++i) {
          if (i->deadline <= now) {
            to_notify.push_back(std::move(i->callback));
          } else {
            max_deadline = std::max(max_deadline, i->deadline);
            if (i != w) {
              *w = std::move(*i);
            }
            ++w;
          }
        }
        if (w != j->second.begin()) {
          j->second.erase(w, j->second.end());
          ++j;
        } else {
          j = lookups.erase(j);
        }
      }
      if (max_deadline == CoarseTimePoint()) {
        it->second.tablet_lookups_by_group.erase(gi);
      }
    }
  }

  for (const auto& callback : to_notify) {
    callback(status);
  }

  if (max_deadline != CoarseTimePoint()) {
    rpc::StartRpc<LookupByKeyRpc>(
        this, table, partition_group_start, max_deadline, client_->data_->messenger_,
        client_->data_->proxy_cache_.get());
  }
}

class LookupByIdRpc : public LookupRpc {
 public:
  LookupByIdRpc(const scoped_refptr<MetaCache>& meta_cache,
                LookupTabletCallback user_cb,
                TabletId tablet_id,
                CoarseTimePoint deadline,
                Messenger* messenger,
                rpc::ProxyCache* proxy_cache)
      : LookupRpc(meta_cache, deadline, messenger, proxy_cache),
        user_cb_(std::move(user_cb)),
        tablet_id_(std::move(tablet_id)) {}

  std::string ToString() const override {
    return Format("LookupByIdRpc(tablet: $0, num_attempts: $1)", tablet_id_, num_attempts());
  }

  void DoSendRpc() override {
    // Fill out the request.
    req_.add_tablet_ids(tablet_id_);

    master_proxy()->GetTabletLocationsAsync(
        req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&LookupByIdRpc::Finished, this, Status::OK()));
  }

 private:
  void Finished(const Status& status) override {
    DoFinished(status, resp_, nullptr /* partition_group_start */);
  }

  void Notify(const Status& status, const RemoteTabletPtr& remote_tablet) override {
    if (status.ok()) {
      user_cb_(remote_tablet);
    } else {
      user_cb_(status);
    }
  }


  // User-specified callback to invoke when the lookup finishes.
  //
  // Always invoked, regardless of success or failure.
  LookupTabletCallback user_cb_;

  // Tablet to lookup.
  TabletId tablet_id_;

  // Request body.
  master::GetTabletLocationsRequestPB req_;

  // Response body.
  master::GetTabletLocationsResponsePB resp_;
};

class LookupByKeyRpc : public LookupRpc {
 public:
  LookupByKeyRpc(const scoped_refptr<MetaCache>& meta_cache,
                 const YBTable* table,
                 MetaCache::PartitionGroupKey partition_group_start,
                 CoarseTimePoint deadline,
                 Messenger* messenger,
                 rpc::ProxyCache* proxy_cache)
      : LookupRpc(meta_cache, deadline, messenger, proxy_cache),
        table_(table->shared_from_this()),
        partition_group_start_(std::move(partition_group_start)) {
  }

  std::string ToString() const override {
    return Format("GetTableLocations($0, $1, $2)",
                  table_->name(),
                  table_->partition_schema()
                      .PartitionKeyDebugString(partition_group_start_,
                                               internal::GetSchema(table_->schema())),
                  num_attempts());
  }

  const YBTableName& table_name() const { return table_->name(); }
  const string& table_id() const { return table_->id(); }

  void DoSendRpc() override {
    // Fill out the request.
    req_.mutable_table()->set_table_id(table_->id());
    req_.set_partition_key_start(partition_group_start_);
    req_.set_max_returned_locations(kPartitionGroupSize);

    // The end partition key is left unset intentionally so that we'll prefetch
    // some additional tablets.
    master_proxy()->GetTableLocationsAsync(
        req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&LookupByKeyRpc::Finished, this, Status::OK()));
  }

 private:
  void Finished(const Status& status) override {
    DoFinished(status, resp_, &partition_group_start_);
  }

  void Notify(const Status& status, const RemoteTabletPtr& result) override {
    if (status.ok()) {
      return; // This case is handled by LookupTabletByKeyFastPathUnlocked.
    }
    meta_cache()->LookupFailed(table_.get(), partition_group_start_, status);
  }

  // Table to lookup.
  std::shared_ptr<const YBTable> table_;

  // Encoded partition key to lookup.
  MetaCache::PartitionGroupKey partition_group_start_;

  // Request body.
  GetTableLocationsRequestPB req_;

  // Response body.
  GetTableLocationsResponsePB resp_;
};

RemoteTabletPtr MetaCache::LookupTabletByKeyFastPathUnlocked(const YBTable* table,
                                                             const std::string& partition_key) {
  auto it = tables_.find(table->id());
  if (PREDICT_FALSE(it == tables_.end())) {
    // No cache available for this table.
    return nullptr;
  }

  DCHECK_EQ(partition_key, table->FindPartitionStart(partition_key));
  auto tablet_it = it->second.tablets_by_partition.find(partition_key);
  if (PREDICT_FALSE(tablet_it == it->second.tablets_by_partition.end())) {
    // No tablets with a start partition key lower than 'partition_key'.
    return nullptr;
  }

  const auto& result = tablet_it->second;

  // Stale entries must be re-fetched.
  if (result->stale()) {
    return nullptr;
  }

  if (result->partition().partition_key_end().compare(partition_key) > 0 ||
      result->partition().partition_key_end().empty()) {
    // partition_key < partition.end OR tablet doesn't end.
    return result;
  }

  return nullptr;
}

// We disable thread safety analysis in this function due to manual conditional locking.
template <class Lock>
bool MetaCache::FastLookupTabletByKeyUnlocked(
    const YBTable* table,
    const std::string& partition_start,
    const LookupTabletCallback& callback,
    Lock* lock) NO_THREAD_SAFETY_ANALYSIS {
  // Fast path: lookup in the cache.
  auto result = LookupTabletByKeyFastPathUnlocked(table, partition_start);
  if (result && result->HasLeader()) {
    lock->unlock();
    VLOG(4) << "Fast lookup: found tablet " << result->tablet_id();
    callback(result);
    return true;
  }

  return false;
}

// We disable thread safety analysis in this function due to manual conditional locking.
void MetaCache::LookupTabletByKey(const YBTable* table,
                                  const string& partition_key,
                                  CoarseTimePoint deadline,
                                  LookupTabletCallback callback) NO_THREAD_SAFETY_ANALYSIS {
  const auto& partition_start = table->FindPartitionStart(partition_key);
  VLOG_WITH_FUNC(4) << "partition_key: " << Slice(partition_key).ToDebugHexString()
                    << " partition_start: " << Slice(partition_start).ToDebugHexString();

  rpc::Rpcs::Handle rpc;
  {
    std::shared_lock<boost::shared_mutex> lock(mutex_);
    if (FastLookupTabletByKeyUnlocked(table, partition_start, callback, &lock)) {
      return;
    }
  }

  const std::string& partition_group_start =
      table->FindPartitionStart(partition_start, kPartitionGroupSize);
  {
    std::unique_lock<boost::shared_mutex> lock(mutex_);
    if (FastLookupTabletByKeyUnlocked(table, partition_start, callback, &lock)) {
      return;
    }

    auto& table_data = tables_[table->id()];
    auto& lookup = table_data.tablet_lookups_by_group[partition_group_start];
    bool was_empty = lookup.empty();
    lookup[partition_start].push_back({std::move(callback), deadline});
    if (!was_empty) {
      return;
    }
  }

  rpc::StartRpc<LookupByKeyRpc>(
      this, table, partition_group_start, deadline, client_->data_->messenger_,
      client_->data_->proxy_cache_.get());
}

RemoteTabletPtr MetaCache::LookupTabletByIdFastPath(const TabletId& tablet_id) {
  SharedLock<decltype(mutex_)> lock(mutex_);
  auto it = tablets_by_id_.find(tablet_id);
  if (it != tablets_by_id_.end()) {
    return it->second;
  }
  return nullptr;
}

void MetaCache::LookupTabletById(const TabletId& tablet_id,
                                 CoarseTimePoint deadline,
                                 LookupTabletCallback callback,
                                 UseCache use_cache) {
  if (use_cache) {
    // Fast path: lookup in the cache.
    scoped_refptr<RemoteTablet> result = LookupTabletByIdFastPath(tablet_id);
    if (result && result->HasLeader()) {
      VLOG(4) << "Fast lookup: found tablet " << result->tablet_id();
      callback(result);
      return;
    }
  }

  rpc::StartRpc<LookupByIdRpc>(
      this, std::move(callback), tablet_id, deadline, client_->data_->messenger_,
      client_->data_->proxy_cache_.get());
}

void MetaCache::MarkTSFailed(RemoteTabletServer* ts,
                             const Status& status) {
  LOG(INFO) << "Marking tablet server " << ts->ToString() << " as failed.";
  SharedLock<decltype(mutex_)> lock(mutex_);

  Status ts_status = status.CloneAndPrepend("TS failed");

  // TODO: replace with a ts->tablet multimap for faster lookup?
  for (const auto& tablet : tablets_by_id_) {
    // We just loop on all tablets; if a tablet does not have a replica on this
    // TS, MarkReplicaFailed() returns false and we ignore the return value.
    tablet.second->MarkReplicaFailed(ts, ts_status);
  }
}

bool MetaCache::AcquireMasterLookupPermit() {
  return master_lookup_sem_.TryAcquire();
}

void MetaCache::ReleaseMasterLookupPermit() {
  master_lookup_sem_.Release();
}

} // namespace internal
} // namespace client
} // namespace yb
