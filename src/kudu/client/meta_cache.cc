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

#include "kudu/client/meta_cache.h"

#include <boost/bind.hpp>
#include <glog/logging.h>

#include "kudu/client/client.h"
#include "kudu/client/client-internal.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/master.pb.h"
#include "kudu/master/master.proxy.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/rpc.h"
#include "kudu/tserver/tserver_service.proxy.h"
#include "kudu/util/net/dns_resolver.h"
#include "kudu/util/net/net_util.h"

using std::string;
using std::map;
using std::shared_ptr;
using strings::Substitute;

namespace kudu {

using consensus::RaftPeerPB;
using master::GetTableLocationsRequestPB;
using master::GetTableLocationsResponsePB;
using master::MasterServiceProxy;
using master::TabletLocationsPB;
using master::TabletLocationsPB_ReplicaPB;
using master::TSInfoPB;
using rpc::Messenger;
using rpc::Rpc;
using tserver::TabletServerServiceProxy;

namespace client {

namespace internal {

////////////////////////////////////////////////////////////

RemoteTabletServer::RemoteTabletServer(const master::TSInfoPB& pb)
  : uuid_(pb.permanent_uuid()) {

  Update(pb);
}

void RemoteTabletServer::DnsResolutionFinished(const HostPort& hp,
                                               vector<Sockaddr>* addrs,
                                               KuduClient* client,
                                               const StatusCallback& user_callback,
                                               const Status &result_status) {
  gscoped_ptr<vector<Sockaddr> > scoped_addrs(addrs);

  Status s = result_status;

  if (s.ok() && addrs->empty()) {
    s = Status::NotFound("No addresses for " + hp.ToString());
  }

  if (!s.ok()) {
    s = s.CloneAndPrepend("Failed to resolve address for TS " + uuid_);
    user_callback.Run(s);
    return;
  }

  VLOG(1) << "Successfully resolved " << hp.ToString() << ": "
          << (*addrs)[0].ToString();

  {
    lock_guard<simple_spinlock> l(&lock_);
    proxy_.reset(new TabletServerServiceProxy(client->data_->messenger_, (*addrs)[0]));
  }
  user_callback.Run(s);
}

void RemoteTabletServer::InitProxy(KuduClient* client, const StatusCallback& cb) {
  HostPort hp;
  {
    unique_lock<simple_spinlock> l(&lock_);

    if (proxy_) {
      // Already have a proxy created.
      l.unlock();
      cb.Run(Status::OK());
      return;
    }

    CHECK(!rpc_hostports_.empty());
    // TODO: if the TS advertises multiple host/ports, pick the right one
    // based on some kind of policy. For now just use the first always.
    hp = rpc_hostports_[0];
  }

  auto addrs = new vector<Sockaddr>();
  client->data_->dns_resolver_->ResolveAddresses(
    hp, addrs, Bind(&RemoteTabletServer::DnsResolutionFinished,
                    Unretained(this), hp, addrs, client, cb));
}

void RemoteTabletServer::Update(const master::TSInfoPB& pb) {
  CHECK_EQ(pb.permanent_uuid(), uuid_);

  lock_guard<simple_spinlock> l(&lock_);

  rpc_hostports_.clear();
  for (const HostPortPB& hostport_pb : pb.rpc_addresses()) {
    rpc_hostports_.push_back(HostPort(hostport_pb.host(), hostport_pb.port()));
  }
}

string RemoteTabletServer::permanent_uuid() const {
  return uuid_;
}

shared_ptr<TabletServerServiceProxy> RemoteTabletServer::proxy() const {
  lock_guard<simple_spinlock> l(&lock_);
  CHECK(proxy_);
  return proxy_;
}

string RemoteTabletServer::ToString() const {
  string ret = uuid_;
  lock_guard<simple_spinlock> l(&lock_);
  if (!rpc_hostports_.empty()) {
    strings::SubstituteAndAppend(&ret, " ($0)", rpc_hostports_[0].ToString());
  }
  return ret;
}

void RemoteTabletServer::GetHostPorts(vector<HostPort>* host_ports) const {
  lock_guard<simple_spinlock> l(&lock_);
  *host_ports = rpc_hostports_;
}

////////////////////////////////////////////////////////////


void RemoteTablet::Refresh(const TabletServerMap& tservers,
                           const google::protobuf::RepeatedPtrField
                             <TabletLocationsPB_ReplicaPB>& replicas) {
  // Adopt the data from the successful response.
  lock_guard<simple_spinlock> l(&lock_);
  replicas_.clear();
  for (const TabletLocationsPB_ReplicaPB& r : replicas) {
    RemoteReplica rep;
    rep.ts = FindOrDie(tservers, r.ts_info().permanent_uuid());
    rep.role = r.role();
    rep.failed = false;
    replicas_.push_back(rep);
  }
  stale_ = false;
}

void RemoteTablet::MarkStale() {
  lock_guard<simple_spinlock> l(&lock_);
  stale_ = true;
}

bool RemoteTablet::stale() const {
  lock_guard<simple_spinlock> l(&lock_);
  return stale_;
}

bool RemoteTablet::MarkReplicaFailed(RemoteTabletServer *ts,
                                     const Status& status) {
  bool found = false;
  lock_guard<simple_spinlock> l(&lock_);
  VLOG(2) << "Tablet " << tablet_id_ << ": Current remote replicas in meta cache: "
          << ReplicasAsStringUnlocked();
  LOG(WARNING) << "Tablet " << tablet_id_ << ": Replica " << ts->ToString()
               << " has failed: " << status.ToString();
  for (RemoteReplica& rep : replicas_) {
    if (rep.ts == ts) {
      rep.failed = true;
      found = true;
    }
  }
  return found;
}

int RemoteTablet::GetNumFailedReplicas() const {
  int failed = 0;
  lock_guard<simple_spinlock> l(&lock_);
  for (const RemoteReplica& rep : replicas_) {
    if (rep.failed) {
      failed++;
    }
  }
  return failed;
}

RemoteTabletServer* RemoteTablet::LeaderTServer() const {
  lock_guard<simple_spinlock> l(&lock_);
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

void RemoteTablet::GetRemoteTabletServers(vector<RemoteTabletServer*>* servers) const {
  lock_guard<simple_spinlock> l(&lock_);
  for (const RemoteReplica& replica : replicas_) {
    if (replica.failed) {
      continue;
    }
    servers->push_back(replica.ts);
  }
}

void RemoteTablet::MarkTServerAsLeader(const RemoteTabletServer* server) {
  bool found = false;
  lock_guard<simple_spinlock> l(&lock_);
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
  lock_guard<simple_spinlock> l(&lock_);
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
  lock_guard<simple_spinlock> l(&lock_);
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

MetaCache::MetaCache(KuduClient* client)
  : client_(client),
    master_lookup_sem_(50) {
}

MetaCache::~MetaCache() {
  STLDeleteValues(&ts_cache_);
}

void MetaCache::UpdateTabletServer(const TSInfoPB& pb) {
  DCHECK(lock_.is_write_locked());
  RemoteTabletServer* ts = FindPtrOrNull(ts_cache_, pb.permanent_uuid());
  if (ts) {
    ts->Update(pb);
    return;
  }

  VLOG(1) << "Client caching new TabletServer " << pb.permanent_uuid();
  InsertOrDie(&ts_cache_, pb.permanent_uuid(), new RemoteTabletServer(pb));
}

// A (table, partition_key) --> tablet lookup. May be in-flight to a master, or
// may be handled locally.
//
// Keeps a reference on the owning metacache while alive.
class LookupRpc : public Rpc {
 public:
  LookupRpc(const scoped_refptr<MetaCache>& meta_cache,
            StatusCallback user_cb,
            const KuduTable* table,
            string partition_key,
            scoped_refptr<RemoteTablet>* remote_tablet,
            const MonoTime& deadline,
            const shared_ptr<Messenger>& messenger);
  virtual ~LookupRpc();
  virtual void SendRpc() OVERRIDE;
  virtual string ToString() const OVERRIDE;

  const GetTableLocationsResponsePB& resp() const { return resp_; }
  const string& table_name() const { return table_->name(); }
  const string& table_id() const { return table_->id(); }

 private:
  virtual void SendRpcCb(const Status& status) OVERRIDE;

  std::shared_ptr<MasterServiceProxy> master_proxy() const {
    return table_->client()->data_->master_proxy();
  }

  void ResetMasterLeaderAndRetry();

  void NewLeaderMasterDeterminedCb(const Status& status);

  // Pointer back to the tablet cache. Populated with location information
  // if the lookup finishes successfully.
  //
  // When the RPC is destroyed, a master lookup permit is returned to the
  // cache if one was acquired in the first place.
  scoped_refptr<MetaCache> meta_cache_;

  // Request body.
  GetTableLocationsRequestPB req_;

  // Response body.
  GetTableLocationsResponsePB resp_;

  // User-specified callback to invoke when the lookup finishes.
  //
  // Always invoked, regardless of success or failure.
  StatusCallback user_cb_;

  // Table to lookup.
  const KuduTable* table_;

  // Encoded partition key to lookup.
  string partition_key_;

  // When lookup finishes successfully, the selected tablet is
  // written here prior to invoking 'user_cb_'.
  scoped_refptr<RemoteTablet> *remote_tablet_;

  // Whether this lookup has acquired a master lookup permit.
  bool has_permit_;
};

LookupRpc::LookupRpc(const scoped_refptr<MetaCache>& meta_cache,
                     StatusCallback user_cb, const KuduTable* table,
                     string partition_key,
                     scoped_refptr<RemoteTablet>* remote_tablet,
                     const MonoTime& deadline,
                     const shared_ptr<Messenger>& messenger)
    : Rpc(deadline, messenger),
      meta_cache_(meta_cache),
      user_cb_(std::move(user_cb)),
      table_(table),
      partition_key_(std::move(partition_key)),
      remote_tablet_(remote_tablet),
      has_permit_(false) {
  DCHECK(deadline.Initialized());
}

LookupRpc::~LookupRpc() {
  if (has_permit_) {
    meta_cache_->ReleaseMasterLookupPermit();
  }
}

void LookupRpc::SendRpc() {
  // Fast path: lookup in the cache.
  scoped_refptr<RemoteTablet> result;
  if (PREDICT_TRUE(meta_cache_->LookupTabletByKeyFastPath(table_, partition_key_, &result)) &&
      result->HasLeader()) {
    VLOG(3) << "Fast lookup: found tablet " << result->tablet_id()
            << " for " << table_->partition_schema()
                                 .PartitionKeyDebugString(partition_key_, *table_->schema().schema_)
            << " of " << table_->name();
    if (remote_tablet_) {
      *remote_tablet_ = result;
    }
    user_cb_.Run(Status::OK());
    delete this;
    return;
  }

  // Slow path: must lookup the tablet in the master.
  VLOG(3) << "Fast lookup: no known tablet"
          << " for " << table_->partition_schema()
                               .PartitionKeyDebugString(partition_key_, *table_->schema().schema_)
          << " of " << table_->name()
          << ": refreshing our metadata from the Master";

  if (!has_permit_) {
    has_permit_ = meta_cache_->AcquireMasterLookupPermit();
  }
  if (!has_permit_) {
    // Couldn't get a permit, try again in a little while.
    mutable_retrier()->DelayedRetry(this, Status::TimedOut(
        "client has too many outstanding requests to the master"));
    return;
  }

  // Fill out the request.
  req_.mutable_table()->set_table_id(table_->id());
  req_.set_partition_key_start(partition_key_);

  // The end partition key is left unset intentionally so that we'll prefetch
  // some additional tablets.

  // See KuduClient::Data::SyncLeaderMasterRpc().
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  if (retrier().deadline().ComesBefore(now)) {
    SendRpcCb(Status::TimedOut("timed out after deadline expired"));
    return;
  }
  MonoTime rpc_deadline = now;
  rpc_deadline.AddDelta(meta_cache_->client_->default_rpc_timeout());
  mutable_retrier()->mutable_controller()->set_deadline(
      MonoTime::Earliest(rpc_deadline, retrier().deadline()));

  master_proxy()->GetTableLocationsAsync(req_, &resp_,
                                         mutable_retrier()->mutable_controller(),
                                         boost::bind(&LookupRpc::SendRpcCb, this, Status::OK()));
}

string LookupRpc::ToString() const {
  return Substitute("GetTableLocations($0, $1, $2)",
                    table_->name(),
                    table_->partition_schema()
                           .PartitionKeyDebugString(partition_key_, *table_->schema().schema_),
                    num_attempts());
}

void LookupRpc::ResetMasterLeaderAndRetry() {
  table_->client()->data_->SetMasterServerProxyAsync(
      table_->client(),
      retrier().deadline(),
      Bind(&LookupRpc::NewLeaderMasterDeterminedCb,
           Unretained(this)));
}

void LookupRpc::NewLeaderMasterDeterminedCb(const Status& status) {
  if (status.ok()) {
    mutable_retrier()->mutable_controller()->Reset();
    SendRpc();
  } else {
    LOG(WARNING) << "Failed to determine new Master: " << status.ToString();
    mutable_retrier()->DelayedRetry(this, status);
  }
}

void LookupRpc::SendRpcCb(const Status& status) {
  gscoped_ptr<LookupRpc> delete_me(this); // delete on scope exit

  // Prefer early failures over controller failures.
  Status new_status = status;
  if (new_status.ok() && mutable_retrier()->HandleResponse(this, &new_status)) {
    ignore_result(delete_me.release());
    return;
  }

  // Prefer controller failures over response failures.
  if (new_status.ok() && resp_.has_error()) {
    if (resp_.error().code() == master::MasterErrorPB::NOT_THE_LEADER ||
        resp_.error().code() == master::MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
      if (meta_cache_->client_->IsMultiMaster()) {
        LOG(WARNING) << "Leader Master has changed, re-trying...";
        ResetMasterLeaderAndRetry();
        ignore_result(delete_me.release());
        return;
      }
    }
    new_status = StatusFromPB(resp_.error().status());
  }

  if (new_status.IsTimedOut()) {
    if (MonoTime::Now(MonoTime::FINE).ComesBefore(retrier().deadline())) {
      if (meta_cache_->client_->IsMultiMaster()) {
        LOG(WARNING) << "Leader Master timed out, re-trying...";
        ResetMasterLeaderAndRetry();
        ignore_result(delete_me.release());
        return;
      }
    } else {
      // Operation deadline expired during this latest RPC.
      new_status = new_status.CloneAndPrepend(
          "timed out after deadline expired");
    }
  }

  if (new_status.IsNetworkError()) {
    if (meta_cache_->client_->IsMultiMaster()) {
      LOG(WARNING) << "Encountered a network error from the Master: " << new_status.ToString()
                     << ", retrying...";
      ResetMasterLeaderAndRetry();
      ignore_result(delete_me.release());
      return;
    }
  }

  // Prefer response failures over no tablets found.
  if (new_status.ok() && resp_.tablet_locations_size() == 0) {
    new_status = Status::NotFound("No such tablet found");
  }

  if (new_status.ok()) {
    const scoped_refptr<RemoteTablet>& result =
        meta_cache_->ProcessLookupResponse(*this);
    if (remote_tablet_) {
      *remote_tablet_ = result;
    }
  } else {
    new_status = new_status.CloneAndPrepend(Substitute("$0 failed", ToString()));
    LOG(WARNING) << new_status.ToString();
  }
  user_cb_.Run(new_status);
}

const scoped_refptr<RemoteTablet>& MetaCache::ProcessLookupResponse(const LookupRpc& rpc) {
  VLOG(2) << "Processing master response for " << rpc.ToString()
          << ". Response: " << rpc.resp().ShortDebugString();

  lock_guard<rw_spinlock> l(&lock_);
  TabletMap& tablets_by_key = LookupOrInsert(&tablets_by_table_and_key_,
                                             rpc.table_id(), TabletMap());
  for (const TabletLocationsPB& loc : rpc.resp().tablet_locations()) {
    // First, update the tserver cache, needed for the Refresh calls below.
    for (const TabletLocationsPB_ReplicaPB& r : loc.replicas()) {
      UpdateTabletServer(r.ts_info());
    }

    // Next, update the tablet caches.
    string tablet_id = loc.tablet_id();
    scoped_refptr<RemoteTablet> remote = FindPtrOrNull(tablets_by_id_, tablet_id);
    if (remote.get() != nullptr) {
      // Partition should not have changed.
      DCHECK_EQ(loc.partition().partition_key_start(), remote->partition().partition_key_start());
      DCHECK_EQ(loc.partition().partition_key_end(), remote->partition().partition_key_end());

      VLOG(3) << "Refreshing tablet " << tablet_id << ": "
              << loc.ShortDebugString();
      remote->Refresh(ts_cache_, loc.replicas());
      continue;
    }

    VLOG(3) << "Caching tablet " << tablet_id << " for (" << rpc.table_name()
            << "): " << loc.ShortDebugString();

    Partition partition;
    Partition::FromPB(loc.partition(), &partition);
    remote = new RemoteTablet(tablet_id, partition);
    remote->Refresh(ts_cache_, loc.replicas());

    InsertOrDie(&tablets_by_id_, tablet_id, remote);
    InsertOrDie(&tablets_by_key, partition.partition_key_start(), remote);
  }

  // Always return the first tablet.
  return FindOrDie(tablets_by_id_, rpc.resp().tablet_locations(0).tablet_id());
}

bool MetaCache::LookupTabletByKeyFastPath(const KuduTable* table,
                                          const string& partition_key,
                                          scoped_refptr<RemoteTablet>* remote_tablet) {
  shared_lock<rw_spinlock> l(&lock_);
  const TabletMap* tablets = FindOrNull(tablets_by_table_and_key_, table->id());
  if (PREDICT_FALSE(!tablets)) {
    // No cache available for this table.
    return false;
  }

  const scoped_refptr<RemoteTablet>* r = FindFloorOrNull(*tablets, partition_key);
  if (PREDICT_FALSE(!r)) {
    // No tablets with a start partition key lower than 'partition_key'.
    return false;
  }

  // Stale entries must be re-fetched.
  if ((*r)->stale()) {
    return false;
  }

  if ((*r)->partition().partition_key_end().compare(partition_key) > 0 ||
      (*r)->partition().partition_key_end().empty()) {
    // partition_key < partition.end OR tablet doesn't end.
    *remote_tablet = *r;
    return true;
  }

  return false;
}

void MetaCache::LookupTabletByKey(const KuduTable* table,
                                  const string& partition_key,
                                  const MonoTime& deadline,
                                  scoped_refptr<RemoteTablet>* remote_tablet,
                                  const StatusCallback& callback) {
  LookupRpc* rpc = new LookupRpc(this,
                                 callback,
                                 table,
                                 partition_key,
                                 remote_tablet,
                                 deadline,
                                 client_->data_->messenger_);
  rpc->SendRpc();
}

void MetaCache::MarkTSFailed(RemoteTabletServer* ts,
                             const Status& status) {
  LOG(INFO) << "Marking tablet server " << ts->ToString() << " as failed.";
  shared_lock<rw_spinlock> l(&lock_);

  Status ts_status = status.CloneAndPrepend("TS failed");

  // TODO: replace with a ts->tablet multimap for faster lookup?
  for (const TabletMap::value_type& tablet : tablets_by_id_) {
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
} // namespace kudu
