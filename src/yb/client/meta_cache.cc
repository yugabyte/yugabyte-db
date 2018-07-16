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

#include <mutex>

#include <boost/bind.hpp>
#include <glog/logging.h>

#include "yb/client/meta_cache.h"

#include "yb/client/client.h"
#include "yb/client/client-internal.h"
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

using std::string;
using std::map;
using std::shared_ptr;
using strings::Substitute;

DEFINE_int32(max_concurrent_master_lookups, 500,
             "Maximum number of concurrent tablet location lookups from YB client to master");

DEFINE_test_flag(bool, verify_all_replicas_alive, false,
                 "If set, when a RemoteTablet object is destroyed, we will verify that all its "
                 "replicas are not marked as failed");

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
using tablet::TabletStatePB;
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

RemoteTabletServer::RemoteTabletServer(
    const string& uuid, const shared_ptr<TabletServerServiceProxy>& proxy)
    : uuid_(uuid), proxy_(proxy) {
}

Status RemoteTabletServer::InitProxy(YBClient* client) {
  std::unique_lock<simple_spinlock> l(lock_);

  if (!dns_resolve_histogram_) {
    auto metric_entity = client->metric_entity();
    if (metric_entity) {
      dns_resolve_histogram_ = METRIC_dns_resolve_latency_during_init_proxy.Instantiate(
          metric_entity);
    }
  }

  if (proxy_) {
    // Already have a proxy created.
    return Status::OK();
  }

  // TODO: if the TS advertises multiple host/ports, pick the right one
  // based on some kind of policy. For now just use the first always.
  auto hostport = HostPortFromPB(DesiredHostPort(
      public_rpc_hostports_, private_rpc_hostports_, cloud_info_pb_,
      client->data_->cloud_info_pb_));
  CHECK(!hostport.host().empty());
  ScopedDnsTracker dns_tracker(dns_resolve_histogram_.get());
  proxy_.reset(new TabletServerServiceProxy(client->data_->proxy_cache_.get(), hostport));

  return Status::OK();
}

void RemoteTabletServer::Update(const master::TSInfoPB& pb) {
  CHECK_EQ(pb.permanent_uuid(), uuid_);

  std::lock_guard<simple_spinlock> l(lock_);

  private_rpc_hostports_ = pb.private_rpc_addresses();
  public_rpc_hostports_ = pb.broadcast_addresses();
  cloud_info_pb_ = pb.cloud_info();
}

bool RemoteTabletServer::IsLocal() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return proxy_ != nullptr && proxy_->proxy().IsServiceLocal();
}

const std::string& RemoteTabletServer::permanent_uuid() const {
  return uuid_;
}

const CloudInfoPB& RemoteTabletServer::cloud_info() const {
  return cloud_info_pb_;
}

shared_ptr<TabletServerServiceProxy> RemoteTabletServer::proxy() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return proxy_;
}

string RemoteTabletServer::ToString() const {
  string ret = "{ uuid: " + uuid_;
  std::lock_guard<simple_spinlock> l(lock_);
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
  std::lock_guard<simple_spinlock> l(lock_);
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

////////////////////////////////////////////////////////////

RemoteTablet::~RemoteTablet() {
  if (PREDICT_FALSE(FLAGS_verify_all_replicas_alive)) {
    // Let's verify that none of the replicas are marked as failed. The test should always wait
    // enough time so that the lookup cache can be refreshed after force_lookup_cache_refresh_secs.
    for (const auto& replica : replicas_) {
      if (replica.failed) {
        LOG(FATAL) << "Remote tablet server " << replica.ts->ToString()
                   << " with role " << consensus::RaftPeerPB::Role_Name(replica.role)
                   << " is marked as failed";
      }
    }
  }
}

void RemoteTablet::Refresh(const TabletServerMap& tservers,
                           const google::protobuf::RepeatedPtrField
                             <TabletLocationsPB_ReplicaPB>& replicas) {
  // Adopt the data from the successful response.
  std::lock_guard<simple_spinlock> l(lock_);
  replicas_.clear();
  for (const TabletLocationsPB_ReplicaPB& r : replicas) {
    RemoteReplica rep;
    auto it = tservers.find(r.ts_info().permanent_uuid());
    CHECK(it != tservers.end());
    rep.ts = it->second.get();
    rep.role = r.role();
    rep.failed = false;
    replicas_.push_back(rep);
  }
  stale_ = false;
}

void RemoteTablet::MarkStale() {
  std::lock_guard<simple_spinlock> l(lock_);
  stale_ = true;
}

bool RemoteTablet::stale() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return stale_;
}

bool RemoteTablet::MarkReplicaFailed(RemoteTabletServer *ts,
                                     const Status& status) {
  std::lock_guard<simple_spinlock> l(lock_);
  VLOG(2) << "Tablet " << tablet_id_ << ": Current remote replicas in meta cache: "
          << ReplicasAsStringUnlocked() << ". Replica " << ts->ToString()
          << " has failed: " << status.ToString();
  for (RemoteReplica& rep : replicas_) {
    if (rep.ts == ts) {
      rep.failed = true;
      return true;
    }
  }
  return false;
}

int RemoteTablet::GetNumFailedReplicas() const {
  int failed = 0;
  std::lock_guard<simple_spinlock> l(lock_);
  for (const RemoteReplica& rep : replicas_) {
    if (rep.failed) {
      failed++;
    }
  }
  return failed;
}

RemoteTabletServer* RemoteTablet::LeaderTServer() const {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const RemoteReplica& replica : replicas_) {
    if (!replica.failed && replica.role == RaftPeerPB::LEADER) {
      return replica.ts;
    }
  }
  return nullptr;
}

bool RemoteTablet::HasLeader() const {
  return LeaderTServer() != nullptr;
}

void RemoteTablet::GetRemoteTabletServers(vector<RemoteTabletServer*>* servers,
                                          UpdateLocalTsState update_local_ts_state) {
  DCHECK(servers->empty());
  std::lock_guard<simple_spinlock> l(lock_);
  for (RemoteReplica& replica : replicas_) {
    if (replica.failed) {
      continue;
    }
    servers->push_back(replica.ts);
  }

  if (!update_local_ts_state || servers->size() == replicas_.size()) {
    return;
  }

  RemoteReplica* local_failed_replica = nullptr;
  for (RemoteReplica& replica : replicas_) {
    if (replica.failed && replica.ts->IsLocal()) {
      local_failed_replica = &replica;
      break;
    }
  }

  if (local_failed_replica == nullptr) {
    return;
  }

  const auto& local_ts_uuid = local_failed_replica->ts->permanent_uuid();

  // If this is the local tablet server, then we will issue an RPC to find out if the state of
  // the tablet has changed, but only if the tablet wasn't in a terminal state.
  auto it = replica_tablet_state_map_.find(local_ts_uuid);

  if (it != replica_tablet_state_map_.end()) {
    switch(it->second) {
      case TabletStatePB::UNKNOWN: FALLTHROUGH_INTENDED;
      case TabletStatePB::NOT_STARTED: FALLTHROUGH_INTENDED;
      case TabletStatePB::BOOTSTRAPPING: FALLTHROUGH_INTENDED;
      case TabletStatePB::RUNNING:
        break;
      case TabletStatePB::FAILED: FALLTHROUGH_INTENDED;
      case TabletStatePB::QUIESCING: FALLTHROUGH_INTENDED;
      case TabletStatePB::SHUTDOWN:
        // These are terminal states, so we won't call GetTabletStatus.
        return;
    }
  }

  tserver::GetTabletStatusRequestPB req;
  tserver::GetTabletStatusResponsePB resp;
  rpc::RpcController rpc;
  req.set_tablet_id(tablet_id_);
  auto status = local_failed_replica->ts->proxy()->GetTabletStatus(req, &resp, &rpc);
  if (!status.ok() || resp.has_error()) {
    LOG(ERROR) << "Received error from GetTabletStatus: "
               << (!status.ok() ? status.ToString() :
                                  StatusFromPB(resp.error().status()).ToString());
    return;
  }

  replica_tablet_state_map_[local_ts_uuid] = resp.tablet_status().state();
  DCHECK_EQ(resp.tablet_status().tablet_id(), tablet_id_);
  VLOG(3) << "GetTabletStatus returned status: "
          << tablet::TabletStatePB_Name(resp.tablet_status().state())
          << " for tablet " << tablet_id_;
  if (resp.tablet_status().state() == tablet::TabletStatePB::RUNNING) {
    VLOG(3) << "Changing state of replica " << local_failed_replica->ts->ToString()
            << " for tablet " << tablet_id_ << " from failed to not failed";

    local_failed_replica->failed = false;
    servers->push_back(local_failed_replica->ts);
  } else {
    VLOG(3) << "Replica " << local_failed_replica->ts->ToString() << " state for tablet "
            << tablet_id_ << " is "
            << tablet::TabletStatePB_Name(resp.tablet_status().state());
  }
}

void RemoteTablet::MarkTServerAsLeader(const RemoteTabletServer* server) {
  bool found = false;
  std::lock_guard<simple_spinlock> l(lock_);
  for (RemoteReplica& replica : replicas_) {
    if (replica.ts == server) {
      replica.role = RaftPeerPB::LEADER;
      found = true;
    } else if (replica.role == RaftPeerPB::LEADER) {
      replica.role = RaftPeerPB::FOLLOWER;
    }
  }
  VLOG(3) << "Latest replicas: " << ReplicasAsStringUnlocked();
  DCHECK(found) << "Tablet " << tablet_id_ << ": Specified server not found: "
                << server->ToString() << ". Replicas: " << ReplicasAsStringUnlocked();
}

void RemoteTablet::MarkTServerAsFollower(const RemoteTabletServer* server) {
  bool found = false;
  std::lock_guard<simple_spinlock> l(lock_);
  for (RemoteReplica& replica : replicas_) {
    if (replica.ts == server) {
      replica.role = RaftPeerPB::FOLLOWER;
      found = true;
    }
  }
  VLOG(3) << "Latest replicas: " << ReplicasAsStringUnlocked();
  DCHECK(found) << "Tablet " << tablet_id_ << ": Specified server not found: "
                << server->ToString() << ". Replicas: " << ReplicasAsStringUnlocked();
}

std::string RemoteTablet::ReplicasAsString() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return ReplicasAsStringUnlocked();
}

std::string RemoteTablet::ReplicasAsStringUnlocked() const {
  DCHECK(lock_.is_locked());
  string replicas_str;
  for (const RemoteReplica& rep : replicas_) {
    if (!replicas_str.empty()) replicas_str += ", ";
    strings::SubstituteAndAppend(&replicas_str, "$0 ($1, $2)",
                                rep.ts->permanent_uuid(),
                                RaftPeerPB::Role_Name(rep.role),
                                rep.failed ? "FAILED" : "OK");
  }
  return replicas_str;
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

void MetaCache::AddTabletServerProxy(const string& permanent_uuid,
                                     const shared_ptr<TabletServerServiceProxy>& proxy) {
  const auto entry = ts_cache_.emplace(permanent_uuid,
                                       std::make_unique<RemoteTabletServer>(permanent_uuid, proxy));
  CHECK(entry.second);
  if (entry.first->second->IsLocal()) {
    local_tserver_ = entry.first->second.get();
  }
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
            const MonoTime& deadline,
            const shared_ptr<Messenger>& messenger,
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
                     const MonoTime& deadline,
                     const shared_ptr<Messenger>& messenger,
                     rpc::ProxyCache* proxy_cache)
    : Rpc(deadline, messenger, proxy_cache),
      meta_cache_(meta_cache),
      retained_self_(meta_cache_->rpcs_.InvalidHandle()) {
  DCHECK(deadline.Initialized());
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
    auto status = mutable_retrier()->DelayedRetry(this,
        STATUS(TryAgain, "Client has too many outstanding requests to the master"));
    LOG_IF(DFATAL, !status.ok()) << "Retry failed: " << status;
    return;
  }

  // See YBClient::Data::SyncLeaderMasterRpc().
  MonoTime now = MonoTime::Now();
  if (retrier().deadline().ComesBefore(now)) {
    Finished(STATUS(TimedOut, "timed out after deadline expired"));
    return;
  }
  MonoTime rpc_deadline = now;
  rpc_deadline.AddDelta(meta_cache_->client_->default_rpc_timeout());
  mutable_retrier()->mutable_controller()->set_deadline(
      MonoTime::Earliest(rpc_deadline, retrier().deadline()));

  DoSendRpc();
}

void LookupRpc::ResetMasterLeaderAndRetry() {
  client()->data_->SetMasterServerProxyAsync(
      client(),
      retrier().deadline(),
      false /* skip_resolution */,
      Bind(&LookupRpc::NewLeaderMasterDeterminedCb,
           Unretained(this)));
}

void LookupRpc::NewLeaderMasterDeterminedCb(const Status& status) {
  if (status.ok()) {
    mutable_retrier()->mutable_controller()->Reset();
    SendRpc();
  } else {
    LOG(WARNING) << "Failed to determine new Master: " << status.ToString();
    auto retry_status = mutable_retrier()->DelayedRetry(this, status);
    LOG_IF(DFATAL, !retry_status.ok()) << "Retry failed: " << retry_status;
  }
}

template <class Response>
void LookupRpc::DoFinished(
    const Status& status, const Response& resp, const std::string* partition_group_start) {
  if (resp.has_error()) {
    LOG(INFO) << "Got resp error " << resp.error().code() << ", code=" << status.CodeAsString();
  }
  // Prefer early failures over controller failures.
  Status new_status = status;
  if (new_status.ok() &&
      mutable_retrier()->HandleResponse(this, &new_status, rpc::RetryWhenBusy::kFalse)) {
    return;
  }

  // Prefer controller failures over response failures.
  if (new_status.ok() && resp.has_error()) {
    if (resp.error().code() == master::MasterErrorPB::NOT_THE_LEADER ||
        resp.error().code() == master::MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
      if (client()->IsMultiMaster()) {
        LOG(WARNING) << "Leader Master has changed, re-trying...";
        ResetMasterLeaderAndRetry();
        return;
      }
    }
    new_status = StatusFromPB(resp.error().status());
  }

  if (new_status.IsTimedOut()) {
    if (MonoTime::Now().ComesBefore(retrier().deadline())) {
      if (client()->IsMultiMaster()) {
        LOG(WARNING) << "Leader Master timed out, re-trying...";
        ResetMasterLeaderAndRetry();
        return;
      }
    } else {
      // Operation deadline expired during this latest RPC.
      new_status = new_status.CloneAndPrepend("timed out after deadline expired");
    }
  }

  if (new_status.IsNetworkError() || new_status.IsRemoteError()) {
    if (client()->IsMultiMaster()) {
      LOG(WARNING) << "Encountered a error from the Master: " << new_status << ", retrying...";
      ResetMasterLeaderAndRetry();
      return;
    }
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
    new_status = new_status.CloneAndPrepend(Substitute("$0 failed", ToString()));
    LOG(WARNING) << new_status.ToString();
    Notify(new_status);
  }
}

RemoteTabletPtr MetaCache::ProcessTabletLocations(
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
    const std::string* partition_group_start) {
  VLOG(2) << "Processing master response " << ToString(locations);

  RemoteTabletPtr result;
  bool first = true;
  std::vector<StatusCallback> to_notify;

  {
    std::lock_guard<decltype(mutex_)> l(mutex_);
    for (const TabletLocationsPB& loc : locations) {
      auto& table_data = tables_[loc.table_id()];
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
        DCHECK_EQ(loc.partition().partition_key_start(), remote->partition().partition_key_start());
        DCHECK_EQ(loc.partition().partition_key_end(), remote->partition().partition_key_end());

        VLOG(3) << "Refreshing tablet " << tablet_id << ": " << loc.ShortDebugString();
      } else {
        VLOG(3) << "Caching tablet " << tablet_id << ": " << loc.ShortDebugString();

        Partition partition;
        Partition::FromPB(loc.partition(), &partition);
        remote = new RemoteTablet(tablet_id, partition);

        CHECK(tablets_by_id_.emplace(tablet_id, remote).second);
        CHECK(tablets_by_key.emplace(partition.partition_key_start(), remote).second);
      }
      remote->Refresh(ts_cache_, loc.replicas());

      if (first) {
        result = remote;
        first = false;
      }

      if (partition_group_start) {
        auto lookup_by_group_iter = table_data.tablet_lookups_by_group.find(*partition_group_start);
        if (lookup_by_group_iter != table_data.tablet_lookups_by_group.end()) {
          auto& lookups_by_partition_key = lookup_by_group_iter->second;
          auto lookups_iter = lookups_by_partition_key.find(loc.partition().partition_key_start());
          if (lookups_iter != lookups_by_partition_key.end()) {
            for (auto& lookup : lookups_iter->second) {
              if (lookup.remote_tablet) {
                *lookup.remote_tablet = remote;
              }
              to_notify.push_back(std::move(lookup.callback));
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

  for (const auto& callback : to_notify) {
    callback.Run(Status::OK());
  }

  CHECK_NOTNULL(result.get());
  return result;
}

void MetaCache::LookupFailed(
    const YBTable* table, const std::string& partition_group_start, const Status& status) {
  VLOG(1) << "Lookup for table " << table->id() << " and partition "
          << Slice(partition_group_start).ToDebugHexString() << ", failed with: " << status;

  std::vector<StatusCallback> to_notify;
  MonoTime max_deadline;
  {
    std::lock_guard<decltype(mutex_)> l(mutex_);
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
      auto now = MonoTime::Now();
      for (auto j = lookups.begin(); j != lookups.end();) {
        auto w = j->second.begin();
        for (auto i = j->second.begin(); i != j->second.end(); ++i) {
          if (i->deadline <= now) {
            to_notify.push_back(std::move(i->callback));
          } else {
            max_deadline.MakeAtLeast(i->deadline);
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
      if (!max_deadline) {
        it->second.tablet_lookups_by_group.erase(gi);
      }
    }
  }

  for (const auto& callback : to_notify) {
    callback.Run(status);
  }

  if (max_deadline) {
    rpc::StartRpc<LookupByKeyRpc>(
        this, table, partition_group_start, max_deadline, client_->data_->messenger_,
        client_->data_->proxy_cache_.get());
  }
}

class LookupByIdRpc : public LookupRpc {
 public:
  LookupByIdRpc(const scoped_refptr<MetaCache>& meta_cache,
                StatusCallback user_cb,
                TabletId tablet_id,
                scoped_refptr<RemoteTablet>* remote_tablet,
                const MonoTime& deadline,
                const shared_ptr<Messenger>& messenger,
                rpc::ProxyCache* proxy_cache)
      : LookupRpc(meta_cache, deadline, messenger, proxy_cache),
        user_cb_(std::move(user_cb)),
        tablet_id_(std::move(tablet_id)),
        remote_tablet_(remote_tablet) {}

  std::string ToString() const override {
    return Format("LookupByIdRpc($0, $1)", tablet_id_, num_attempts());
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

  void Notify(const Status& status, const RemoteTabletPtr& result) override {
    if (remote_tablet_ && result) {
      *remote_tablet_ = result;
    }
    user_cb_.Run(status);
  }


  // User-specified callback to invoke when the lookup finishes.
  //
  // Always invoked, regardless of success or failure.
  StatusCallback user_cb_;

  // Tablet to lookup.
  TabletId tablet_id_;

  // When lookup finishes successfully, the selected tablet is
  // written here prior to invoking 'user_cb_'.
  scoped_refptr<RemoteTablet>* remote_tablet_;

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
                 const MonoTime& deadline,
                 const shared_ptr<Messenger>& messenger,
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

template <class Lock>
bool MetaCache::FastLookupTabletByKeyUnlocked(
    const YBTable* table,
    const std::string& partition_start,
    RemoteTabletPtr* remote_tablet,
    const StatusCallback& callback,
    Lock* lock) {
  // Fast path: lookup in the cache.
  auto result = LookupTabletByKeyFastPathUnlocked(table, partition_start);
  if (result && result->HasLeader()) {
    lock->unlock();
    VLOG(3) << "Fast lookup: found tablet " << result->tablet_id();
    if (remote_tablet) {
      *remote_tablet = result;
    }
    callback.Run(Status::OK());
    return true;
  }

  return false;
}

void MetaCache::LookupTabletByKey(const YBTable* table,
                                  const string& partition_key,
                                  const MonoTime& deadline,
                                  RemoteTabletPtr* remote_tablet,
                                  const StatusCallback& callback) {
  const auto& partition_start = table->FindPartitionStart(partition_key);

  rpc::Rpcs::Handle rpc;
  {
    boost::shared_lock<boost::shared_mutex> lock(mutex_);
    if (FastLookupTabletByKeyUnlocked(table, partition_start, remote_tablet, callback, &lock)) {
      return;
    }
  }

  const std::string& partition_group_start =
      table->FindPartitionStart(partition_start, kPartitionGroupSize);
  {
    std::unique_lock<boost::shared_mutex> lock(mutex_);
    if (FastLookupTabletByKeyUnlocked(table, partition_start, remote_tablet, callback, &lock)) {
      return;
    }

    auto& table_data = tables_[table->id()];
    auto& lookup = table_data.tablet_lookups_by_group[partition_group_start];
    bool was_empty = lookup.empty();
    lookup[partition_start].push_back({callback, remote_tablet, deadline});
    if (!was_empty) {
      return;
    }
  }

  rpc::StartRpc<LookupByKeyRpc>(
      this, table, partition_group_start, deadline, client_->data_->messenger_,
      client_->data_->proxy_cache_.get());
}

RemoteTabletPtr MetaCache::LookupTabletByIdFastPath(const TabletId& tablet_id) {
  boost::shared_lock<decltype(mutex_)> l(mutex_);
  auto it = tablets_by_id_.find(tablet_id);
  if (it != tablets_by_id_.end()) {
    return it->second;
  }
  return nullptr;
}

void MetaCache::LookupTabletById(const TabletId& tablet_id,
                                 const MonoTime& deadline,
                                 RemoteTabletPtr* remote_tablet,
                                 const StatusCallback& callback,
                                 bool use_fast_path_first) {
  if (use_fast_path_first) {
    // Fast path: lookup in the cache.
    scoped_refptr<RemoteTablet> result = LookupTabletByIdFastPath(tablet_id);
    if (result && result->HasLeader()) {
      VLOG(3) << "Fast lookup: found tablet " << result->tablet_id();
      if (remote_tablet) {
        *remote_tablet = result;
      }
      callback.Run(Status::OK());
      return;
    }
  }

  rpc::StartRpc<LookupByIdRpc>(
      this, callback, tablet_id, remote_tablet, deadline, client_->data_->messenger_,
      client_->data_->proxy_cache_.get());
}

void MetaCache::MarkTSFailed(RemoteTabletServer* ts,
                             const Status& status) {
  LOG(INFO) << "Marking tablet server " << ts->ToString() << " as failed.";
  boost::shared_lock<decltype(mutex_)> lock(mutex_);

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
