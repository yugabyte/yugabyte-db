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

#include <stdint.h>

#include <atomic>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_io.hpp>

#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/client_master_rpc.h"
#include "yb/client/client-internal.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_client.proxy.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/local_tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/unique_lock.h"

using std::map;
using std::shared_ptr;
using std::string;
using strings::Substitute;
using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_int32(max_concurrent_master_lookups, 500,
             "Maximum number of concurrent tablet location lookups from YB client to master");

DEFINE_test_flag(bool, verify_all_replicas_alive, false,
                 "If set, when a RemoteTablet object is destroyed, we will verify that all its "
                 "replicas are not marked as failed");

DEFINE_UNKNOWN_int32(retry_failed_replica_ms, 60 * 1000,
             "Time in milliseconds to wait for before retrying a failed replica");

DEFINE_UNKNOWN_int64(meta_cache_lookup_throttling_step_ms, 5,
             "Step to increment delay between calls during lookup throttling.");

DEFINE_UNKNOWN_int64(meta_cache_lookup_throttling_max_delay_ms, 1000,
             "Max delay between calls during lookup throttling.");

DEFINE_test_flag(bool, force_master_lookup_all_tablets, false,
                 "If set, force the client to go to the master for all tablet lookup "
                 "instead of reading from cache.");

DEFINE_test_flag(int32, sleep_before_metacache_lookup_ms, 0,
                 "If set, will sleep in LookupTabletByKey for a random amount up to this value.");
DEFINE_test_flag(double, simulate_lookup_timeout_probability, 0,
                 "If set, mark an RPC as failed and force retry on the first attempt.");
DEFINE_test_flag(double, simulate_lookup_partition_list_mismatch_probability, 0,
                 "Probability for simulating the partition list mismatch error on tablet lookup.");

METRIC_DEFINE_event_stats(
  server, dns_resolve_latency_during_init_proxy,
  "yb.client.MetaCache.InitProxy DNS Resolve",
  yb::MetricUnit::kMicroseconds,
  "Microseconds spent resolving DNS requests during MetaCache::InitProxy");

DECLARE_string(placement_cloud);
DECLARE_string(placement_region);

namespace yb {

using master::GetTableLocationsRequestPB;
using master::GetTableLocationsResponsePB;
using master::TabletLocationsPB;
using master::TabletLocationsPB_ReplicaPB;
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

std::atomic<int64_t> lookup_serial_{1};

} // namespace

int64_t TEST_GetLookupSerial() {
  return lookup_serial_.load(std::memory_order_acquire);
}

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

RemoteTabletServer::~RemoteTabletServer() = default;

Status RemoteTabletServer::InitProxy(YBClient* client) {
  {
    SharedLock lock(mutex_);

    if (proxy_) {
      // Already have a proxy created.
      return Status::OK();
    }
  }

  std::lock_guard lock(mutex_);

  if (proxy_) {
    // Already have a proxy created.
    return Status::OK();
  }

  if (!dns_resolve_stats_) {
    auto metric_entity = client->metric_entity();
    if (metric_entity) {
      dns_resolve_stats_ = METRIC_dns_resolve_latency_during_init_proxy.Instantiate(
          metric_entity);
    }
  }

  // TODO: if the TS advertises multiple host/ports, pick the right one
  // based on some kind of policy. For now just use the first always.
  auto hostport = HostPortFromPB(yb::DesiredHostPort(
      public_rpc_hostports_, private_rpc_hostports_, cloud_info_pb_,
      client->data_->cloud_info_pb_));
  CHECK(!hostport.host().empty());
  ScopedDnsTracker dns_tracker(dns_resolve_stats_.get());
  proxy_.reset(new TabletServerServiceProxy(client->data_->proxy_cache_.get(), hostport));
  proxy_endpoint_ = hostport;

  return Status::OK();
}

void RemoteTabletServer::Update(const master::TSInfoPB& pb) {
  CHECK_EQ(pb.permanent_uuid(), uuid_);

  std::lock_guard lock(mutex_);
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

shared_ptr<TabletServerServiceProxy> RemoteTabletServer::proxy() const {
  SharedLock lock(mutex_);
  return proxy_;
}

::yb::HostPort RemoteTabletServer::ProxyEndpoint() const {
  SharedLock lock(mutex_);
  return proxy_endpoint_;
}

string RemoteTabletServer::ToString() const {
  string ret = "{ uuid: " + uuid_;
  SharedLock lock(mutex_);
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
  SharedLock lock(mutex_);
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
  SharedLock lock(mutex_);
  return std::binary_search(capabilities_.begin(), capabilities_.end(), capability);
}

bool RemoteTabletServer::IsLocalRegion() const {
  SharedLock lock(mutex_);
  return cloud_info_pb_.placement_cloud() == FLAGS_placement_cloud &&
         cloud_info_pb_.placement_region() == FLAGS_placement_region;
}

LocalityLevel RemoteTabletServer::LocalityLevelWith(const CloudInfoPB& cloud_info) const {
  SharedLock lock(mutex_);
  return PlacementInfoConverter::GetLocalityLevel(cloud_info_pb_, cloud_info);
}

HostPortPB RemoteTabletServer::DesiredHostPort(const CloudInfoPB& cloud_info) const {
  SharedLock lock(mutex_);
  return yb::DesiredHostPort(
      public_rpc_hostports_, private_rpc_hostports_, cloud_info_pb_, cloud_info);
}

std::string RemoteTabletServer::TEST_PlacementZone() const {
  SharedLock lock(mutex_);
  return cloud_info_pb_.placement_zone();
}

std::string ReplicasCount::ToString() {
  return Format(
      " live replicas $0, read replicas $1, expected live replicas $2, expected read replicas $3",
      num_alive_live_replicas, num_alive_read_replicas,
      expected_live_replicas, expected_read_replicas);
}

////////////////////////////////////////////////////////////

RemoteTablet::RemoteTablet(std::string tablet_id,
                           dockv::Partition partition,
                           boost::optional<PartitionListVersion> partition_list_version,
                           uint64 split_depth,
                           const TabletId& split_parent_tablet_id)
    : tablet_id_(std::move(tablet_id)),
      log_prefix_(Format("T $0: ", tablet_id_)),
      partition_(std::move(partition)),
      partition_list_version_(partition_list_version),
      split_depth_(split_depth),
      split_parent_tablet_id_(split_parent_tablet_id),
      stale_(false) {
}

RemoteTablet::~RemoteTablet() {
  if (PREDICT_FALSE(FLAGS_TEST_verify_all_replicas_alive)) {
    // Let's verify that none of the replicas are marked as failed. The test should always wait
    // enough time so that the lookup cache can be refreshed after force_lookup_cache_refresh_secs.
    for (const auto& replica : replicas_) {
      if (replica->Failed()) {
        LOG_WITH_PREFIX(FATAL) << "Remote tablet server " << replica->ts->ToString()
                               << " with role " << PeerRole_Name(replica->role)
                               << " is marked as failed";
      }
    }
  }
}

void RemoteTablet::Refresh(
    const TabletServerMap& tservers,
    const google::protobuf::RepeatedPtrField<TabletLocationsPB_ReplicaPB>& replicas) {
  // Adopt the data from the successful response.
  std::lock_guard lock(mutex_);
  std::vector<std::string> old_uuids;
  old_uuids.reserve(replicas_.size());
  for (const auto& replica : replicas_) {
    old_uuids.push_back(replica->ts->permanent_uuid());
  }
  std::sort(old_uuids.begin(), old_uuids.end());
  replicas_.clear();
  bool has_new_replica = false;
  for (const TabletLocationsPB_ReplicaPB& r : replicas) {
    auto it = tservers.find(r.ts_info().permanent_uuid());
    CHECK(it != tservers.end());
    replicas_.emplace_back(std::make_shared<RemoteReplica>(it->second.get(), r.role()));
    has_new_replica =
        has_new_replica ||
        !std::binary_search(old_uuids.begin(), old_uuids.end(), r.ts_info().permanent_uuid());
  }
  if (has_new_replica) {
    lookups_without_new_replicas_ = 0;
  } else {
    ++lookups_without_new_replicas_;
  }
  stale_ = false;
  refresh_time_.store(MonoTime::Now(), std::memory_order_release);
}

void RemoteTablet::MarkStale() {
  std::lock_guard lock(mutex_);
  stale_ = true;
}

bool RemoteTablet::stale() const {
  SharedLock lock(mutex_);
  return stale_;
}

void RemoteTablet::MarkAsSplit() {
  std::lock_guard lock(mutex_);
  is_split_ = true;
}

bool RemoteTablet::is_split() const {
  SharedLock lock(mutex_);
  return is_split_;
}

bool RemoteTablet::MarkReplicaFailed(RemoteTabletServer *ts, const Status& status) {
  std::lock_guard lock(mutex_);
  VLOG_WITH_PREFIX(2) << "Current remote replicas in meta cache: "
                      << ReplicasAsStringUnlocked() << ". Replica " << ts->ToString()
                      << " has failed: " << status.ToString();
  for (auto& rep : replicas_) {
    if (rep->ts == ts) {
      rep->MarkFailed();
      return true;
    }
  }
  return false;
}

int RemoteTablet::GetNumFailedReplicas() const {
  int failed = 0;
  SharedLock lock(mutex_);
  for (const auto& rep : replicas_) {
    if (rep->Failed()) {
      failed++;
    }
  }
  return failed;
}

bool RemoteTablet::IsReplicasCountConsistent() const {
  return replicas_count_.load(std::memory_order_acquire).IsReplicasCountConsistent();
}

string RemoteTablet::ReplicasCountToString() const {
  return replicas_count_.load(std::memory_order_acquire).ToString();
}

void RemoteTablet::SetExpectedReplicas(int expected_live_replicas, int expected_read_replicas) {
  ReplicasCount old_replicas_count = replicas_count_.load(std::memory_order_acquire);
  for (;;) {
    ReplicasCount new_replicas_count = old_replicas_count;
    new_replicas_count.SetExpectedReplicas(expected_live_replicas, expected_read_replicas);
    if (replicas_count_.compare_exchange_weak(
        old_replicas_count, new_replicas_count, std::memory_order_acq_rel)) {
      break;
    }
  }
}

void RemoteTablet::SetAliveReplicas(int alive_live_replicas, int alive_read_replicas) {
  ReplicasCount old_replicas_count = replicas_count_.load(std::memory_order_acquire);
  for (;;) {
    ReplicasCount new_replicas_count = old_replicas_count;
    new_replicas_count.SetAliveReplicas(alive_live_replicas, alive_read_replicas);
    if (replicas_count_.compare_exchange_weak(
        old_replicas_count, new_replicas_count, std::memory_order_acq_rel)) {
      break;
    }
  }
}

RemoteTabletServer* RemoteTablet::LeaderTServer() const {
  SharedLock lock(mutex_);
  for (const auto& replica : replicas_) {
    if (!replica->Failed() && replica->role == PeerRole::LEADER) {
      return replica->ts;
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
    std::shared_ptr<RemoteReplica> replica;
    tablet::RaftGroupStatePB new_state;
    bool clear_failed;
  };
  std::vector<ReplicaUpdate> replica_updates;
  {
    SharedLock lock(mutex_);
    int num_alive_live_replicas = 0;
    int num_alive_read_replicas = 0;
    for (auto& replica : replicas_) {
      if (replica->Failed()) {
        if (include_failed_replicas) {
          servers->push_back(replica->ts);
          continue;
        }
        ReplicaUpdate replica_update = {replica, RaftGroupStatePB::UNKNOWN, false};
        VLOG_WITH_PREFIX(4)
            << "Replica " << replica->ts->ToString()
            << " failed, state: " << RaftGroupStatePB_Name(replica->state)
            << ", is local: " << replica->ts->IsLocal()
            << ", time since failure: " << (MonoTime::Now() - replica->last_failed_time);
        switch (replica->state) {
          case RaftGroupStatePB::UNKNOWN: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::NOT_STARTED: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::BOOTSTRAPPING: FALLTHROUGH_INTENDED;
          case RaftGroupStatePB::RUNNING:
            // These are non-terminal states that may retry. Check and update failed local replica's
            // current state. For remote replica, just wait for some time before retrying.
            if (replica->ts->IsLocal()) {
              tserver::GetTabletStatusRequestPB req;
              tserver::GetTabletStatusResponsePB resp;
              req.set_tablet_id(tablet_id_);
              const Status status =
                  CHECK_NOTNULL(replica->ts->local_tserver())->GetTabletStatus(&req, &resp);
              if (!status.ok() || resp.has_error()) {
                LOG_WITH_PREFIX(ERROR)
                    << "Received error from GetTabletStatus: "
                    << (!status.ok() ? status : StatusFromPB(resp.error().status()));
                continue;
              }
              if (resp.tablet_status().is_hidden()) {
                // Should continue here because otherwise failed state will be cleared.
                VLOG_WITH_PREFIX(3) << "Tablet is hidden";
                continue;
              }

              DCHECK_EQ(resp.tablet_status().tablet_id(), tablet_id_);
              VLOG_WITH_PREFIX(3) << "GetTabletStatus returned status: "
                                  << tablet::RaftGroupStatePB_Name(resp.tablet_status().state())
                                  << " for replica " << replica->ts->ToString();
              replica_update.new_state = resp.tablet_status().state();
              if (replica_update.new_state != tablet::RaftGroupStatePB::RUNNING) {
                if (replica_update.new_state != replica->state) {
                  // Cannot update replica here directly because holding only shared lock on mutex.
                  replica_updates.push_back(replica_update); // Update only state
                }
                continue;
              }
              if (!replica->ts->local_tserver()->LeaderAndReady(
                      tablet_id_, /* allow_stale */ true)) {
                // Should continue here because otherwise failed state will be cleared.
                continue;
              }
            } else if ((MonoTime::Now() - replica->last_failed_time) <
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

        VLOG_WITH_PREFIX(3) << "Changing state of replica " << replica->ts->ToString()
                            << " from failed to not failed";
        replica_update.clear_failed = true;
        // Cannot update replica here directly because holding only shared lock on mutex.
        replica_updates.push_back(replica_update);
      } else {
        if (replica->role == PeerRole::READ_REPLICA) {
          num_alive_read_replicas++;
        } else if (replica->role == PeerRole::FOLLOWER || replica->role == PeerRole::LEADER) {
          num_alive_live_replicas++;
        }
      }
      servers->push_back(replica->ts);
    }
    SetAliveReplicas(num_alive_live_replicas, num_alive_read_replicas);
  }
  if (!replica_updates.empty()) {
    std::lock_guard lock(mutex_);
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

bool RemoteTablet::IsLocalRegion() {
  auto tservers = GetRemoteTabletServers(internal::IncludeFailedReplicas::kTrue);
  for (const auto &tserver : tservers) {
    if (!tserver->IsLocalRegion()) {
      return false;
    }
  }
  return true;
}

bool RemoteTablet::MarkTServerAsLeader(const RemoteTabletServer* server) {
  bool found = false;
  std::lock_guard lock(mutex_);
  for (auto& replica : replicas_) {
    if (replica->ts == server) {
      replica->role = PeerRole::LEADER;
      found = true;
    } else if (replica->role == PeerRole::LEADER) {
      replica->role = PeerRole::FOLLOWER;
    }
  }
  VLOG_WITH_PREFIX(3) << "Latest replicas: " << ReplicasAsStringUnlocked();
  VLOG_IF_WITH_PREFIX(3, !found) << "Specified server not found: " << server->ToString()
                                 << ". Replicas: " << ReplicasAsStringUnlocked();
  return found;
}

void RemoteTablet::MarkTServerAsFollower(const RemoteTabletServer* server) {
  bool found = false;
  std::lock_guard lock(mutex_);
  for (auto& replica : replicas_) {
    if (replica->ts == server) {
      replica->role = PeerRole::FOLLOWER;
      found = true;
    }
  }
  VLOG_WITH_PREFIX(3) << "Latest replicas: " << ReplicasAsStringUnlocked();
  DCHECK(found) << "Tablet " << tablet_id_ << ": Specified server not found: "
                << server->ToString() << ". Replicas: " << ReplicasAsStringUnlocked();
}

std::string RemoteTablet::ReplicasAsString() const {
  SharedLock lock(mutex_);
  return ReplicasAsStringUnlocked();
}

std::string RemoteTablet::ReplicasAsStringUnlocked() const {
  DCHECK(mutex_.is_locked());
  string replicas_str;
  for (const auto& rep : replicas_) {
    if (!replicas_str.empty()) replicas_str += ", ";
    replicas_str += rep->ToString();
  }
  return replicas_str;
}

std::string RemoteTablet::ToString() const {
  return YB_CLASS_TO_STRING(tablet_id, partition, partition_list_version, split_depth);
}

PartitionListVersion RemoteTablet::GetLastKnownPartitionListVersion() const {
  SharedLock lock(mutex_);
  return last_known_partition_list_version_;
}

void RemoteTablet::MakeLastKnownPartitionListVersionAtLeast(
    PartitionListVersion partition_list_version) {
  std::lock_guard lock(mutex_);
  last_known_partition_list_version_ =
      std::max(last_known_partition_list_version_, partition_list_version);
}

void LookupCallbackVisitor::operator()(const LookupTabletCallback& tablet_callback) const {
  if (error_status_) {
    tablet_callback(*error_status_);
    return;
  }
  auto remote_tablet = boost::get<RemoteTabletPtr>(param_);
  if (remote_tablet == nullptr) {
    static const Status error_status = STATUS(
        TryAgain, "Tablet for requested partition is not yet running",
        ClientError(ClientErrorCode::kTabletNotYetRunning));
    tablet_callback(error_status);
    return;
  }
  tablet_callback(remote_tablet);
}

void LookupCallbackVisitor::operator()(
    const LookupTabletRangeCallback& tablet_range_callback) const {
  if (error_status_) {
    tablet_range_callback(*error_status_);
    return;
  }
  auto result = boost::get<std::vector<RemoteTabletPtr>>(param_);
  tablet_range_callback(result);
}

////////////////////////////////////////////////////////////

MetaCache::MetaCache(YBClient* client)
  : client_(client),
    master_lookup_sem_(FLAGS_max_concurrent_master_lookups),
    log_prefix_(Format("MetaCache($0)(client_id: $1): ", static_cast<void*>(this), client_->id())) {
}

MetaCache::~MetaCache() {
}

void MetaCache::SetLocalTabletServer(const string& permanent_uuid,
                                     const shared_ptr<TabletServerServiceProxy>& proxy,
                                     const LocalTabletServer* local_tserver) {
  const auto entry = ts_cache_.emplace(permanent_uuid,
                                       std::make_shared<RemoteTabletServer>(permanent_uuid,
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

  VLOG_WITH_PREFIX(1) << "Client caching new TabletServer " << permanent_uuid;
  CHECK(ts_cache_.emplace(permanent_uuid, std::make_shared<RemoteTabletServer>(pb)).second);
}

// A (table, partition_key) --> tablet lookup. May be in-flight to a master, or
// may be handled locally.
//
// Keeps a reference on the owning metacache while alive.
class LookupRpc : public internal::ClientMasterRpcBase, public RequestCleanup {
 public:
  LookupRpc(const scoped_refptr<MetaCache>& meta_cache,
            const std::shared_ptr<const YBTable>& table,
            int64_t request_no,
            CoarseTimePoint deadline);

  virtual ~LookupRpc();

  void SendRpc() override;

  MetaCache* meta_cache() { return meta_cache_.get(); }
  YBClient* client() const { return meta_cache_->client_; }

  virtual void NotifyFailure(const Status& status) = 0;

  template <class Response>
  void DoProcessResponse(const Status& status, const Response& resp);

  // Subclasses can override VerifyResponse for implementing additional response checks. Called
  // from Finished if there are no errors passed in response.
  virtual Status VerifyResponse() { return Status::OK(); }

  int64_t request_no() const {
    return request_no_;
  }

  const std::shared_ptr<const YBTable>& table() const {
    return table_;
  }

  // When we receive a response for a key lookup or full table rpc, add callbacks to be fired off
  // to the to_notify set corresponding to the rpc type. Modify tables pointer by removing lookups
  // as we add to the to_notify set.
  virtual void AddCallbacksToBeNotified(
      const ProcessedTablesMap& processed_tables,
      std::unordered_map<TableId, TableData>* tables,
      std::vector<std::pair<LookupCallback, LookupCallbackVisitor>>* to_notify) = 0;

  // When looking up by key or full table, update the processe table with the returned location.
  virtual void UpdateProcessedTable(const TabletLocationsPB& loc,
                                    RemoteTabletPtr remote,
                                    ProcessedTablesMap::mapped_type* processed_table) = 0;

 private:
  virtual Status ProcessTabletLocations(
     const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
     boost::optional<PartitionListVersion> table_partition_list_version) = 0;

  const int64_t request_no_;

  // Pointer back to the tablet cache. Populated with location information
  // if the lookup finishes successfully.
  //
  // When the RPC is destroyed, a master lookup permit is returned to the
  // cache if one was acquired in the first place.
  scoped_refptr<MetaCache> meta_cache_;

  // Table to lookup. Optional in case lookup by ID, but if specified used to check if table
  // partitions are stale.
  const std::shared_ptr<const YBTable> table_;

  // Whether this lookup has acquired a master lookup permit.
  bool has_permit_ = false;
};

LookupRpc::LookupRpc(const scoped_refptr<MetaCache>& meta_cache,
                     const std::shared_ptr<const YBTable>& table,
                     int64_t request_no,
                     CoarseTimePoint deadline)
    : ClientMasterRpcBase(meta_cache->client_, deadline),
      request_no_(request_no),
      meta_cache_(meta_cache),
      table_(table) {
  DCHECK(deadline != CoarseTimePoint());
}

LookupRpc::~LookupRpc() {
  if (has_permit_) {
    meta_cache_->ReleaseMasterLookupPermit();
  }
}

void LookupRpc::SendRpc() {
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
    Finished(STATUS_FORMAT(TimedOut, "Timed out after deadline expired, passed: $0",
                           MonoDelta(now - retrier().start())));
    return;
  }
  mutable_retrier()->PrepareController();

  ClientMasterRpcBase::SendRpc();
}

namespace {

Status GetFirstErrorForTabletById(const master::GetTabletLocationsResponsePB& resp) {
  return resp.errors_size() > 0 ? StatusFromPB(resp.errors(0).status()) : Status::OK();
}

Status GetFirstErrorForTabletById(const master::GetTableLocationsResponsePB& resp) {
  // There are no per-tablet lookup errors inside GetTableLocationsResponsePB.
  return Status::OK();
}

template <class Response>
boost::optional<PartitionListVersion> GetPartitionListVersion(const Response& resp) {
  return resp.has_partition_list_version()
             ? boost::make_optional<PartitionListVersion>(resp.partition_list_version())
             : boost::none;
}

} // namespace

template <class Response>
void LookupRpc::DoProcessResponse(const Status& status, const Response& resp) {
  auto new_status = status;
  if (new_status.ok()) {
    new_status = VerifyResponse();
  }

  // Prefer response failures over no tablets found.
  if (new_status.ok()) {
    new_status = GetFirstErrorForTabletById(resp);
  }

  if (new_status.ok() && resp.tablet_locations_size() == 0) {
    new_status = STATUS(NotFound, "No such tablet found");
  }

  if (new_status.ok()) {
    if (RandomActWithProbability(FLAGS_TEST_simulate_lookup_timeout_probability)) {
      const auto s = STATUS(TimedOut, "Simulate timeout for test.");
      NotifyFailure(s);
      return;
    }
    new_status = ProcessTabletLocations(resp.tablet_locations(), GetPartitionListVersion(resp));
  }
  if (!new_status.ok()) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 1) << new_status;
    new_status = new_status.CloneAndPrepend(Substitute("$0 failed", ToString()));
    NotifyFailure(new_status);
  }
}

namespace {

Status CheckTabletLocations(
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
    AllowSplitTablet allow_split_tablets) {
  const std::string* prev_partition_end = nullptr;
  for (const TabletLocationsPB& loc : locations) {
    LOG_IF(DFATAL, !allow_split_tablets && loc.split_tablet_ids().size() > 0)
        << "Processing remote tablet location with split children id set: "
        << loc.ShortDebugString() << " when allow_split_tablets was set to false.";
    if (prev_partition_end && *prev_partition_end > loc.partition().partition_key_start()) {
      LOG(DFATAL) << "There should be no overlaps in tablet partitions and they should be sorted "
                  << "by partition_key_start. Prev partition end: "
                  << Slice(*prev_partition_end).ToDebugHexString() << ", current partition start: "
                  << Slice(loc.partition().partition_key_start()).ToDebugHexString()
                  << ". Tablet locations: " << [&locations] {
                       std::string result;
                       for (auto& loc : locations) {
                         result += "\n  " + AsString(loc);
                       }
                       return result;
                     }();
      return STATUS(IllegalState, "Wrong order or overlaps in partitions");
    }
    prev_partition_end = &loc.partition().partition_key_end();
  }
  return Status::OK();
}

class TabletIdLookup : public ToStringable {
 public:
  explicit TabletIdLookup(const TabletId& tablet_id) : tablet_id_(tablet_id) {}

  std::string ToString() const override {
    return Format("Tablet: $0", tablet_id_);
  }

 private:
  const TabletId& tablet_id_;
};

class TablePartitionLookup : public ToStringable {
 public:
  explicit TablePartitionLookup(
      const TableId& table_id, const VersionedPartitionGroupStartKey& partition_group_start)
      : table_id_(table_id), partition_group_start_(partition_group_start) {}

  std::string ToString() const override {
    return Format("Table: $0, partition: $1, partition list version: $2",
                  table_id_, Slice(*partition_group_start_.key).ToDebugHexString(),
                  partition_group_start_.partition_list_version);
  }

 private:
  const TableId& table_id_;
  const VersionedPartitionGroupStartKey partition_group_start_;
};

class FullTableLookup : public ToStringable {
 public:
  explicit FullTableLookup(const TableId& table_id)
      : table_id_(table_id) {}

  std::string ToString() const override {
    return Format("FullTableLookup: $0", table_id_);
  }
 private:
  const TableId& table_id_;
};

} // namespace

Status MetaCache::ProcessTabletLocations(
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
    boost::optional<PartitionListVersion> table_partition_list_version, LookupRpc* lookup_rpc,
    AllowSplitTablet allow_split_tablets) {
  if (VLOG_IS_ON(2)) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "lookup_rpc: " << AsString(lookup_rpc);
    for (const auto& loc : locations) {
      for (const auto& table_id : loc.table_ids()) {
        VLOG_WITH_PREFIX_AND_FUNC(2) << loc.tablet_id() << ", " << table_id;
      }
    }
    VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(locations);
  }

  RETURN_NOT_OK(CheckTabletLocations(locations, allow_split_tablets));

  std::vector<std::pair<LookupCallback, LookupCallbackVisitor>> to_notify;
  {
    std::lock_guard lock(mutex_);
    ProcessedTablesMap processed_tables;

    for (const TabletLocationsPB& loc : locations) {
      auto remote = VERIFY_RESULT(ProcessTabletLocation(
          loc, &processed_tables, table_partition_list_version, lookup_rpc));

      auto it = tablet_lookups_by_id_.find(loc.tablet_id());
      if (it != tablet_lookups_by_id_.end()) {
        while (auto* lookup = it->second.lookups.Pop()) {
          to_notify.emplace_back(std::move(lookup->callback),
                                 LookupCallbackVisitor(remote));
          delete lookup;
        }
      }
    }
    if (lookup_rpc) {
      lookup_rpc->AddCallbacksToBeNotified(processed_tables, &tables_, &to_notify);
      lookup_rpc->CleanupRequest();
    }
  }

  for (const auto& callback_and_param : to_notify) {
    boost::apply_visitor(callback_and_param.second, callback_and_param.first);
  }

  return Status::OK();
}

Result<RemoteTabletPtr> MetaCache::ProcessTabletLocation(
    const TabletLocationsPB& location, ProcessedTablesMap* processed_tables,
    const boost::optional<PartitionListVersion>& table_partition_list_version,
    LookupRpc* lookup_rpc) {
  const std::string& tablet_id = location.tablet_id();

  RemoteTabletPtr remote = FindPtrOrNull(tablets_by_id_, tablet_id);

  // First, update the tserver cache, needed for the Refresh calls below.
  for (const TabletLocationsPB_ReplicaPB& r : location.replicas()) {
    UpdateTabletServerUnlocked(r.ts_info());
  }

  VersionedTablePartitionListPtr colocated_table_partition_list;
  if (location.table_ids_size() > 1 && lookup_rpc && lookup_rpc->table()) {
    // When table_ids_size() == 1 we only receive info for the single table from the master
    // and we already have TableData initialized for it (this is done before sending an RPC to
    // the master). And when table_ids_size() > 1, it means we got response for lookup RPC for
    // co-located table and we can re-use TableData::partition_list from the table that was
    // requested by MetaCache::LookupTabletByKey caller for other tables co-located with this
    // one (since all co-located tables sharing the same set of tablets have the same table
    // partition list and now we have list of them returned by the master).
    const auto lookup_table_it = tables_.find(lookup_rpc->table()->id());
    if (lookup_table_it != tables_.end()) {
      colocated_table_partition_list = lookup_table_it->second.partition_list;
    } else {
      // We don't want to crash the server in that case for production, since this is not a
      // correctness issue, but gives some performance degradation on first lookups for
      // co-located tables.
      // But we do want it to crash in debug, so we can more reliably catch this if it happens.
      LOG_WITH_PREFIX(DFATAL) << Format(
          "Internal error: got response for lookup RPC for co-located table, but MetaCache "
          "table data wasn't initialized with partition list for this table. RPC: $0",
          AsString(lookup_rpc));
    }
  }

  for (const std::string& table_id : location.table_ids()) {
    auto& processed_table = (*processed_tables)[table_id];
    std::map<PartitionKey, RemoteTabletPtr>* tablets_by_key = nullptr;

    auto table_it = tables_.find(table_id);
    if (table_it == tables_.end() && location.table_ids_size() > 1 &&
        colocated_table_partition_list) {
      table_it = InitTableDataUnlocked(table_id, colocated_table_partition_list);
    }
    if (table_it != tables_.end()) {
      auto& table_data = table_it->second;

      const auto msg_formatter = [&] {
        return Format(
            "Received table $0 partitions version: $1, MetaCache's table partitions version: "
            "$2",
            table_id, table_partition_list_version, table_data.partition_list->version);
      };
      VLOG_WITH_PREFIX_AND_FUNC(4) << msg_formatter();
      if (table_partition_list_version.has_value()) {
        if (table_partition_list_version.get() != table_data.partition_list->version) {
          return STATUS(
              TryAgain, msg_formatter(),
              ClientError(ClientErrorCode::kTablePartitionListIsStale));
        }
        // We need to guarantee that table_data.tablets_by_partition cache corresponds to
        // table_data.partition_list (see comments for TableData::partitions).
        // So, we don't update tablets_by_partition cache if we don't know table partitions
        // version for both response and TableData.
        // This only can happen for those LookupTabletById requests that don't specify table,
        // because they don't care about partitions changing.
        tablets_by_key = &table_data.tablets_by_partition;
      }
    }

    // Next, update the tablet caches.
    if (location.is_deleted()) {
      VLOG_WITH_PREFIX(5) << "Marking tablet " << tablet_id << " as deleted";

      tablet_lookups_by_id_.erase(tablet_id);
      tablets_by_id_.erase(tablet_id);
      deleted_tablets_.insert(tablet_id);
      return RemoteTabletPtr();
    }

    if (remote) {
      // For colocated tables, RemoteTablet already exists because it was processed in a previous
      // iteration of the for loop (for location.table_ids()). Assert that the partition splits
      // are still the same.
      DCHECK_EQ(location.partition().partition_key_start(),
                remote->partition().partition_key_start());
      DCHECK_EQ(location.partition().partition_key_end(),
                remote->partition().partition_key_end());

      VLOG_WITH_PREFIX(5) << "Refreshing tablet " << tablet_id << ": "
                          << location.ShortDebugString() << " if not split.";
    } else {
      VLOG_WITH_PREFIX(5) << "Caching tablet " << tablet_id << ": "
                          << location.ShortDebugString() << " if not split.";

      dockv::Partition partition;
      dockv::Partition::FromPB(location.partition(), &partition);
      remote = new RemoteTablet(
          tablet_id, partition, table_partition_list_version, location.split_depth(),
          location.split_parent_tablet_id());

      CHECK(tablets_by_id_.emplace(tablet_id, remote).second);
    }
    // Add this tablet to the current table's tablets_by_key map.
    if (tablets_by_key) {
      if (location.split_tablet_ids().size() == 0) {
        (*tablets_by_key)[remote->partition().partition_key_start()] = remote;
      } else {
        // We should not update the partition cache with the remote tablet if it has been split.
        // Also, we cannot return TABLET_SPLIT error since use cases like x-cluster and cdc access
        // the parent tablet by id after the split has been processed.
        VLOG_WITH_PREFIX(5) << "Skipped caching tablet " << tablet_id << " by key since it has "
                            << "been split: " << yb::ToString(location.split_tablet_ids());
      }
    }
    remote->Refresh(ts_cache_, location.replicas());
    remote->SetExpectedReplicas(location.expected_live_replicas(),
                                location.expected_read_replicas());
    if (table_partition_list_version.has_value()) {
      remote->MakeLastKnownPartitionListVersionAtLeast(*table_partition_list_version);
    }
    if (lookup_rpc) {
      lookup_rpc->UpdateProcessedTable(location, remote, &processed_table);
    }
  }

  return remote;
}

std::unordered_map<TableId, TableData>::iterator MetaCache::InitTableDataUnlocked(
    const TableId& table_id, const VersionedTablePartitionListPtr& partitions) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << Format(
      "MetaCache initializing TableData ($0 tables) for table $1: $2, "
      "partition_list_version: $3",
      tables_.size(), table_id, tables_.count(table_id), partitions->version);
  return tables_.emplace(
      std::piecewise_construct, std::forward_as_tuple(table_id),
      std::forward_as_tuple(partitions)).first;
}

void MetaCache::InvalidateTableCache(const YBTable& table) {
  const auto& table_id = table.id();
  const auto table_partition_list = table.GetVersionedPartitions();
  VLOG_WITH_PREFIX_AND_FUNC(1) << Format(
      "table: $0, table.partition_list.version: $1", table_id, table_partition_list->version);

  std::vector<LookupCallback> to_notify;

  auto invalidate_needed = [this, &table_id, &table_partition_list](const auto& it) {
    const auto table_data_partition_list_version = it->second.partition_list->version;
    VLOG_WITH_PREFIX(1) << Format(
        "tables_[$0].partition_list.version: $1", table_id, table_data_partition_list_version);
    // Only need to invalidate table cache, if it is has partition list version older than table's
    // one.
    return table_data_partition_list_version < table_partition_list->version;
  };

  {
    SharedLock<decltype(mutex_)> lock(mutex_);
    auto it = tables_.find(table_id);
    if (it != tables_.end()) {
      if (!invalidate_needed(it)) {
        return;
      }
    }
  }

  {
    UniqueLock<decltype(mutex_)> lock(mutex_);
    auto it = tables_.find(table_id);
    if (it != tables_.end()) {
      if (!invalidate_needed(it)) {
        return;
      }
    } else {
      it = InitTableDataUnlocked(table_id, table_partition_list);
      // Nothing to invalidate, we have just initiliazed it first time.
      return;
    }

    auto& table_data = it->second;

    // Some partitions could be mapped to tablets that have been split and we need to re-fetch
    // info about tablets serving partitions.
    for (auto& tablet : table_data.tablets_by_partition) {
      tablet.second->MarkStale();
    }

    for (auto& tablet : table_data.all_tablets) {
      tablet->MarkStale();
    }
    // TODO(tsplit): Optimize to retry only necessary lookups inside ProcessTabletLocations,
    // detect which need to be retried by GetTableLocationsResponsePB.partition_list_version.
    for (auto& group_lookups : table_data.tablet_lookups_by_group) {
      while (auto* lookup = group_lookups.second.lookups.Pop()) {
        to_notify.push_back(std::move(lookup->callback));
        delete lookup;
      }
    }
    table_data.tablet_lookups_by_group.clear();

    // Only update partitions here after invalidating TableData cache to avoid inconsistencies.
    // See https://github.com/yugabyte/yugabyte-db/issues/6890.
    table_data.partition_list = table_partition_list;
  }
  for (const auto& callback : to_notify) {
    const auto s = STATUS_EC_FORMAT(
        TryAgain, ClientError(ClientErrorCode::kMetaCacheInvalidated),
        "MetaCache for table $0 has been invalidated.", table_id);
    boost::apply_visitor(LookupCallbackVisitor(s), callback);
  }
}

std::shared_ptr<RemoteTabletServer> MetaCache::GetRemoteTabletServer(
    const std::string& permanent_uuid) {
  SharedLock lock(mutex_);
  auto it = ts_cache_.find(permanent_uuid);
  if (it != ts_cache_.end()) {
    return it->second;
  }
  return nullptr;
}

class MetaCache::CallbackNotifier {
 public:
  explicit CallbackNotifier(const Status& status) : status_(status) {}

  void Add(LookupCallback&& callback) {
    callbacks_.push_back(std::move(callback));
  }

  void SetStatus(const Status& status) {
    status_ = status;
  }

  ~CallbackNotifier() {
    for (const auto& callback : callbacks_) {
      boost::apply_visitor(LookupCallbackVisitor(status_), callback);
    }
  }
 private:
  std::vector<LookupCallback> callbacks_;
  Status status_;
};

CoarseTimePoint MetaCache::LookupFailed(
    const Status& status, int64_t request_no, const ToStringable& lookup_id,
    LookupDataGroup* lookup_data_group,
    CallbackNotifier* notifier) {
  std::vector<LookupData*> retry;
  auto now = CoarseMonoClock::Now();
  CoarseTimePoint max_deadline;

  while (auto* lookup = lookup_data_group->lookups.Pop()) {
    if (!status.IsTimedOut() || lookup->deadline <= now) {
      notifier->Add(std::move(lookup->callback));
      delete lookup;
    } else {
      max_deadline = std::max(max_deadline, lookup->deadline);
      retry.push_back(lookup);
    }
  }

  if (retry.empty()) {
    lookup_data_group->Finished(request_no, lookup_id);
  } else {
    for (auto* lookup : retry) {
      lookup_data_group->lookups.Push(lookup);
    }
  }

  return max_deadline;
}

class LookupByIdRpc : public LookupRpc {
 public:
  LookupByIdRpc(const scoped_refptr<MetaCache>& meta_cache,
                const TabletId& tablet_id,
                const std::shared_ptr<const YBTable>& table,
                master::IncludeInactive include_inactive,
                master::IncludeDeleted include_deleted,
                int64_t request_no,
                CoarseTimePoint deadline,
                int64_t lookups_without_new_replicas)
      : LookupRpc(meta_cache, table, request_no, deadline),
        tablet_id_(tablet_id),
        include_inactive_(include_inactive),
        include_deleted_(include_deleted) {
    if (lookups_without_new_replicas != 0) {
      send_delay_ = std::min(
          lookups_without_new_replicas * FLAGS_meta_cache_lookup_throttling_step_ms,
          FLAGS_meta_cache_lookup_throttling_max_delay_ms) * 1ms;
    }
  }

  std::string ToString() const override {
    return Format("LookupByIdRpc(tablet: $0, num_attempts: $1)", tablet_id_, num_attempts());
  }

  void SendRpc() override {
    if (send_delay_) {
      auto delay = send_delay_;
      send_delay_ = MonoDelta();
      auto status = mutable_retrier()->DelayedRetry(this, Status::OK(), delay);
      if (!status.ok()) {
        Finished(status);
      }
      return;
    }

    LookupRpc::SendRpc();
  }

  void CallRemoteMethod() override {
    // Fill out the request.
    req_.clear_tablet_ids();
    req_.add_tablet_ids(tablet_id_);
    if (table()) {
      req_.set_table_id(table()->id());
    }
    req_.set_include_inactive(include_inactive_);
    req_.set_include_deleted(include_deleted_);

    master_client_proxy()->GetTabletLocationsAsync(
        req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&LookupByIdRpc::Finished, this, Status::OK()));
  }

  void AddCallbacksToBeNotified(
    const ProcessedTablesMap& processed_tables,
    std::unordered_map<TableId, TableData>* tables,
    std::vector<std::pair<LookupCallback, LookupCallbackVisitor>>* to_notify) override {
    // Nothing to do when looking up by id.
    return;
  }

  void UpdateProcessedTable(const TabletLocationsPB& loc,
                            RemoteTabletPtr remote,
                            ProcessedTablesMap::mapped_type* processed_table) override {
    return;
  }

 private:
  void ProcessResponse(const Status& status) override {
    DoProcessResponse(status, resp_);
  }

  Status ResponseStatus() override {
    return StatusFromResp(resp_);
  }

  void CleanupRequest() override NO_THREAD_SAFETY_ANALYSIS {
    auto& lookups = meta_cache()->tablet_lookups_by_id_;
    auto it = lookups.find(tablet_id_);
    TabletIdLookup tablet_id_lookup(tablet_id_);
    if (it != lookups.end()) {
      it->second.Finished(request_no(), tablet_id_lookup);
    } else {
      LOG_WITH_PREFIX(INFO) << "Cleanup request for unknown tablet: "
                            << tablet_id_lookup.ToString();
    }
  }

  void NotifyFailure(const Status& status) override {
    meta_cache()->LookupByIdFailed(
        tablet_id_, table(), include_inactive_, include_deleted_,
        GetPartitionListVersion(resp_), request_no(), status);
  }

  Status ProcessTabletLocations(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
      boost::optional<PartitionListVersion> table_partition_list_version) override {
    // Use cases like x-cluster and cdc access the split parent tablet explicitly by id post split.
    // Hence we expect to see split tablets in the response.
    return meta_cache()->ProcessTabletLocations(
        locations, table_partition_list_version, this, AllowSplitTablet::kTrue);
  }

  // Tablet to lookup.
  const TabletId tablet_id_;

  // Whether or not to lookup inactive (hidden) tablets.
  master::IncludeInactive include_inactive_;

  // Whether or not to return deleted tablets.
  master::IncludeDeleted include_deleted_;

  // Request body.
  master::GetTabletLocationsRequestPB req_;

  // Response body.
  master::GetTabletLocationsResponsePB resp_;

  MonoDelta send_delay_;
};

class LookupFullTableRpc : public LookupRpc {
 public:
  LookupFullTableRpc(const scoped_refptr<MetaCache>& meta_cache,
                     const std::shared_ptr<const YBTable>& table,
                     int64_t request_no,
                     CoarseTimePoint deadline)
      : LookupRpc(meta_cache, table, request_no, deadline) {
  }

  std::string ToString() const override {
    return Format("LookupFullTableRpc($0, $1)", table()->name(), num_attempts());
  }

  void CallRemoteMethod() override {
    // Fill out the request.
    req_.mutable_table()->set_table_id(table()->id());
    req_.set_max_returned_locations(std::numeric_limits<int32_t>::max());
    // The end partition key is left unset intentionally so that we'll prefetch
    // some additional tablets.
    master_client_proxy()->GetTableLocationsAsync(
        req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&LookupFullTableRpc::Finished, this, Status::OK()));
  }

  void AddCallbacksToBeNotified(
      const ProcessedTablesMap& processed_tables,
      std::unordered_map<TableId, TableData>* tables,
      std::vector<std::pair<LookupCallback, LookupCallbackVisitor>>* to_notify) override {
    for (const auto& processed_table : processed_tables) {
      // Handle tablet range
      const auto it = tables->find(processed_table.first);
      if (it == tables->end()) {
        continue;
      }
      auto& table_data = it->second;
      auto& full_table_lookups = table_data.full_table_lookups;
      while (auto* lookup = full_table_lookups.lookups.Pop()) {
        std::vector<internal::RemoteTabletPtr> remote_tablets;
        for (const auto& entry : processed_table.second) {
          remote_tablets.push_back(entry.second);
        }
        table_data.all_tablets = remote_tablets;
        to_notify->emplace_back(std::move(lookup->callback),
                                LookupCallbackVisitor(std::move(remote_tablets)));
        delete lookup;
      }
    }
  }

  void UpdateProcessedTable(const TabletLocationsPB& loc,
                            RemoteTabletPtr remote,
                            ProcessedTablesMap::mapped_type* processed_table) override {
    processed_table->emplace(loc.partition().partition_key_start(), remote);
  }

 private:
  void ProcessResponse(const Status& status) override {
    DoProcessResponse(status, resp_);
  }

  Status ResponseStatus() override {
    return StatusFromResp(resp_);
  }

  void CleanupRequest() override NO_THREAD_SAFETY_ANALYSIS {
    const auto it = meta_cache()->tables_.find(table()->id());
    if (it == meta_cache()->tables_.end()) {
      return;
    }
    auto& table_data = it->second;
    auto full_table_lookup = FullTableLookup(table()->id());
    table_data.full_table_lookups.Finished(request_no(), full_table_lookup, false);
  }

  void NotifyFailure(const Status& status) override {
    meta_cache()->LookupFullTableFailed(table(), request_no(), status);
  }

  Status ProcessTabletLocations(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
      boost::optional<PartitionListVersion> table_partition_list_version) override {
    // On LookupFullTableRpc, master reads from the active 'partitions_' map, so it would never
    // return location(s) containing split_tablet_ids.
    return meta_cache()->ProcessTabletLocations(
        locations, table_partition_list_version, this, AllowSplitTablet::kFalse);
  }

  // Request body.
  GetTableLocationsRequestPB req_;

  // Response body.
  GetTableLocationsResponsePB resp_;
};

class LookupByKeyRpc : public LookupRpc {
 public:
  LookupByKeyRpc(const scoped_refptr<MetaCache>& meta_cache,
                 const std::shared_ptr<const YBTable>& table,
                 const VersionedPartitionGroupStartKey& partition_group_start,
                 int64_t request_no,
                 CoarseTimePoint deadline)
      : LookupRpc(meta_cache, table, request_no, deadline),
        partition_group_start_(partition_group_start) {
  }

  std::string ToString() const override {
    return Format(
        "GetTableLocations { table_name: $0, table_id: $1, partition_start_key: $2, "
        "partition_list_version: $3, "
        "request_no: $4, num_attempts: $5 }",
        table()->name(),
        table()->id(),
        table()->partition_schema().PartitionKeyDebugString(
            *partition_group_start_.key, internal::GetSchema(table()->schema())),
        partition_group_start_.partition_list_version,
        request_no(),
        num_attempts());
  }

  const string& table_id() const { return table()->id(); }

  void CallRemoteMethod() override {
    // Fill out the request.
    req_.mutable_table()->set_table_id(table()->id());
    req_.set_partition_key_start(*partition_group_start_.key);
    req_.set_max_returned_locations(kPartitionGroupSize);

    // The end partition key is left unset intentionally so that we'll prefetch
    // some additional tablets.
    master_client_proxy()->GetTableLocationsAsync(
        req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&LookupByKeyRpc::Finished, this, Status::OK()));
  }

  void AddCallbacksToBeNotified(
      const ProcessedTablesMap& processed_tables,
      std::unordered_map<TableId, TableData>* tables,
      std::vector<std::pair<LookupCallback, LookupCallbackVisitor>>* to_notify) override {
    for (const auto& processed_table : processed_tables) {
      const auto table_it = tables->find(processed_table.first);
      if (table_it == tables->end()) {
        continue;
      }
      auto& table_data = table_it->second;
      // This should be guaranteed by ProcessTabletLocations before we get here:
      DCHECK(table_data.partition_list->version == partition_group_start_.partition_list_version)
          << "table_data.partition_list->version: " << table_data.partition_list->version
          << " partition_group_start_.partition_list_version: "
          << partition_group_start_.partition_list_version;
      const auto lookup_by_group_iter =
          table_data.tablet_lookups_by_group.find(*partition_group_start_.key);
      if (lookup_by_group_iter != table_data.tablet_lookups_by_group.end()) {
        VLOG_WITH_PREFIX_AND_FUNC(4)
            << "Checking tablet_lookups_by_group for partition_group_start: "
            << Slice(*partition_group_start_.key).ToDebugHexString();
        auto& lookups_group = lookup_by_group_iter->second;
        while (auto* lookup = lookups_group.lookups.Pop()) {
          auto remote_it = processed_table.second.find(*lookup->partition_start);
          auto lookup_visitor = LookupCallbackVisitor(
              remote_it != processed_table.second.end() ? remote_it->second : nullptr);
          to_notify->emplace_back(std::move(lookup->callback), lookup_visitor);
          delete lookup;
        }
      }
    }
  }

  void UpdateProcessedTable(const TabletLocationsPB& loc,
                            RemoteTabletPtr remote,
                            ProcessedTablesMap::mapped_type* processed_table) override {
    processed_table->emplace(loc.partition().partition_key_start(), remote);
  }

 private:
  void ProcessResponse(const Status& status) override {
    DoProcessResponse(status, resp_);
  }

  Status ResponseStatus() override {
    return StatusFromResp(resp_);
  }

  Status VerifyResponse() override {
    // Note: if LookupByIdRpc response has no partition list version this means this response
    // is from master that doesn't support tablet splitting and it is OK to treat it as version 0
    // (and 0 return value of resp_.partition_list_version() is OK).
    const auto req_partition_list_version = partition_group_start_.partition_list_version;
    const auto resp_partition_list_version = resp_.partition_list_version();

    const auto versions_formatter = [&] {
      return Format(
          "RPC: $0, response partition_list_version: $1", this->ToString(),
          resp_partition_list_version);
    };

    if (resp_partition_list_version < req_partition_list_version) {
      // This means an issue on master side, table partition list version shouldn't decrease on
      // master.
      const auto msg = Format("Ignoring response for obsolete RPC call. $0", versions_formatter());
      LOG_WITH_PREFIX(DFATAL) << msg;
      return STATUS(IllegalState, msg);
    }
    if (resp_partition_list_version > req_partition_list_version) {
      // This request is for partition_group_start calculated based on obsolete table partition
      // list.
      const auto msg = Format(
          "Cached table partition list version is obsolete, refresh required. $0",
          versions_formatter());
      VLOG_WITH_PREFIX_AND_FUNC(3) << msg;
      return STATUS(TryAgain, msg, ClientError(ClientErrorCode::kTablePartitionListIsStale));
    }
    // resp_partition_list_version == req_partition_list_version
    return Status::OK();
  }

  void CleanupRequest() override NO_THREAD_SAFETY_ANALYSIS {
    const auto it = meta_cache()->tables_.find(table()->id());
    if (it == meta_cache()->tables_.end()) {
      return;
    }
    auto& table_data = it->second;
    // This should be guaranteed by ProcessTabletLocations before we get here:
    DCHECK(table_data.partition_list->version == partition_group_start_.partition_list_version)
        << "table_data.partition_list->version: " << table_data.partition_list->version
        << " partition_group_start_.partition_list_version: "
        << partition_group_start_.partition_list_version;
    const auto lookup_by_group_iter = table_data.tablet_lookups_by_group.find(
        *partition_group_start_.key);
    TablePartitionLookup tablet_partition_lookup(table()->id(), partition_group_start_);
    if (lookup_by_group_iter != table_data.tablet_lookups_by_group.end()) {
      lookup_by_group_iter->second.Finished(request_no(), tablet_partition_lookup, false);
    } else {
      LOG_WITH_PREFIX(INFO) << "Cleanup request for unknown partition group: "
                << tablet_partition_lookup.ToString();
    }
  }

  void NotifyFailure(const Status& status) override {
    meta_cache()->LookupByKeyFailed(
        table(), partition_group_start_, resp_.partition_list_version(), request_no(), status);
  }

  Status ProcessTabletLocations(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
      boost::optional<PartitionListVersion> table_partition_list_version) override {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "partition_group_start: " << partition_group_start_.ToString();
    // This condition is guaranteed by VerifyResponse function:
    CHECK(resp_.partition_list_version() == partition_group_start_.partition_list_version);
    // On LookupByKeyRpc, master reads from the active 'partitions_' map, so it would never
    // return location(s) containing split_tablet_ids.
    return meta_cache()->ProcessTabletLocations(
        locations, table_partition_list_version, this, AllowSplitTablet::kFalse);
  }

  // Encoded partition group start key to lookup.
  VersionedPartitionGroupStartKey partition_group_start_;

  // Request body.
  GetTableLocationsRequestPB req_;

  // Response body.
  GetTableLocationsResponsePB resp_;
};

void MetaCache::LookupByKeyFailed(
    const std::shared_ptr<const YBTable>& table,
    const VersionedPartitionGroupStartKey& partition_group_start,
    PartitionListVersion response_partition_list_version,
    int64_t request_no, const Status& status) {
  const auto req_partition_list_version = partition_group_start.partition_list_version;
  VLOG_WITH_PREFIX(1) << "Lookup for table " << table->id() << " and partition group start "
                      << Slice(*partition_group_start.key).ToDebugHexString()
                      << ", request partition list version: " << req_partition_list_version
                      << ", response partition list version: " << response_partition_list_version
                      << " failed with: " << status;

  CallbackNotifier notifier(status);
  CoarseTimePoint max_deadline;
  {
    std::lock_guard lock(mutex_);
    auto it = tables_.find(table->id());
    if (it == tables_.end()) {
      return;
    }

    auto& table_data = it->second;
    const auto table_data_partition_list_version = table_data.partition_list->version;
    const auto versions_formatter = [&] {
      return Format(
          "MetaCache's table $0 partition list version: $1, stored in RPC call: $2, received: $3",
          table->id(), table_data_partition_list_version,
          req_partition_list_version, response_partition_list_version);
    };

    if (table_data_partition_list_version < req_partition_list_version) {
      // MetaCache partition list version is older than stored in LookupByKeyRpc for which we've
      // received an answer.
      // This shouldn't happen, because MetaCache partition list version for each table couldn't
      // decrease and we store it inside LookupByKeyRpc when creating an RPC.
      LOG_WITH_PREFIX(FATAL) << Format(
          "Cached table partition list version is older than stored in RPC call. $0",
          versions_formatter());
    } else if (table_data_partition_list_version > req_partition_list_version) {
      // MetaCache table  partition list version has updated since we've sent this RPC.
      // We've already failed and cleaned all registered lookups for the table on MetaCache table
      // partition list update, so we should ignore responses as well.
      VLOG_WITH_PREFIX_AND_FUNC(3) << Format(
          "Cached table partition list version is newer than stored in RPC call, ignoring "
          "failure response. $0", versions_formatter());
      return;
    }

    // Since table_data_partition_list_version == req_partition_list_version,
    // we will get tablet lookups for this exact RPC call there:
    auto key_lookup_iterator = table_data.tablet_lookups_by_group.find(*partition_group_start.key);
    if (key_lookup_iterator != table_data.tablet_lookups_by_group.end()) {
      auto& lookup_data_group = key_lookup_iterator->second;
      max_deadline = LookupFailed(status, request_no,
                                  TablePartitionLookup(table->id(), partition_group_start),
                                  &lookup_data_group, &notifier);
    }
  }

  if (max_deadline != CoarseTimePoint()) {
    auto rpc = std::make_shared<LookupByKeyRpc>(
        this, table, partition_group_start, request_no, max_deadline);
    client_->data_->rpcs_.RegisterAndStart(rpc, rpc->RpcHandle());
  }
}

void MetaCache::LookupFullTableFailed(const std::shared_ptr<const YBTable>& table,
                                      int64_t request_no, const Status& status) {
  VLOG_WITH_PREFIX(1) << "Lookup for table " << table->id() << " failed with: " << status;

  CallbackNotifier notifier(status);
  CoarseTimePoint max_deadline;
  {
    std::lock_guard lock(mutex_);
    auto it = tables_.find(table->id());
    if (it == tables_.end()) {
      return;
    }

    max_deadline = LookupFailed(status, request_no, FullTableLookup(table->id()),
                                &it->second.full_table_lookups, &notifier);
  }

  if (max_deadline != CoarseTimePoint()) {
    auto rpc = std::make_shared<LookupFullTableRpc>(this, table, request_no, max_deadline);
    client_->data_->rpcs_.RegisterAndStart(rpc, rpc->RpcHandle());
  }
}

void MetaCache::LookupByIdFailed(
    const TabletId& tablet_id,
    const std::shared_ptr<const YBTable>& table,
    master::IncludeInactive include_inactive,
    master::IncludeDeleted include_deleted,
    const boost::optional<PartitionListVersion>& response_partition_list_version,
    int64_t request_no,
    const Status& status) {
  VLOG_WITH_PREFIX(1) << "Lookup for tablet " << tablet_id << ", failed with: " << status;

  CallbackNotifier notifier(status);
  CoarseTimePoint max_deadline;
  {
    std::lock_guard lock(mutex_);
    if (status.IsNotFound() && response_partition_list_version.has_value()) {
      auto tablet = LookupTabletByIdFastPathUnlocked(tablet_id);
      if (tablet && *tablet) {
        const auto tablet_last_known_table_partition_list_version =
            (*tablet)->GetLastKnownPartitionListVersion();
        if (tablet_last_known_table_partition_list_version <
            response_partition_list_version.value()) {
          const auto msg_formatter = [&] {
            return Format(
                "Received table $0 ($1) partitions version: $2, last known by MetaCache for "
                "tablet $3: $4",
                table->id(), table->name(), response_partition_list_version,
                tablet_id, tablet_last_known_table_partition_list_version);
          };
          VLOG_WITH_PREFIX_AND_FUNC(3) << msg_formatter();
          notifier.SetStatus(STATUS(
              TryAgain, msg_formatter(), ClientError(ClientErrorCode::kTablePartitionListIsStale)));
        }
      }
    }

    auto table_lookup_iterator = tablet_lookups_by_id_.find(tablet_id);
    if (table_lookup_iterator != tablet_lookups_by_id_.end()) {
      auto& lookup_data_group = table_lookup_iterator->second;
      max_deadline = LookupFailed(status, request_no, TabletIdLookup(tablet_id),
                                  &lookup_data_group, &notifier);
    }
  }

  if (max_deadline != CoarseTimePoint()) {
    auto rpc = std::make_shared<LookupByIdRpc>(
        this, tablet_id, table, include_inactive, include_deleted, request_no, max_deadline, 0);
    client_->data_->rpcs_.RegisterAndStart(rpc, rpc->RpcHandle());
  }
}

RemoteTabletPtr MetaCache::LookupTabletByKeyFastPathUnlocked(
    const TableId& table_id, const VersionedPartitionStartKey& versioned_partition_start_key) {
  auto it = tables_.find(table_id);
  if (PREDICT_FALSE(it == tables_.end())) {
    // No cache available for this table.
    return nullptr;
  }

  const auto& table_data = it->second;

  if (PREDICT_FALSE(
          table_data.partition_list->version !=
          versioned_partition_start_key.partition_list_version)) {
    // TableData::partition_list version in cache does not match partition_list_version used to
    // calculate partition_key_start, can't use cache.
    return nullptr;
  }

  const auto& partition_start_key = *versioned_partition_start_key.key;

  DCHECK_EQ(
      partition_start_key,
      *client::FindPartitionStart(table_data.partition_list, partition_start_key));
  auto tablet_it = table_data.tablets_by_partition.find(partition_start_key);
  if (PREDICT_FALSE(tablet_it == it->second.tablets_by_partition.end())) {
    // No tablets with a start partition key lower than 'partition_key'.
    return nullptr;
  }

  const auto& result = tablet_it->second;

  // Stale entries must be re-fetched.
  if (result->stale()) {
    return nullptr;
  }

  if (result->partition().partition_key_end().compare(partition_start_key) > 0 ||
      result->partition().partition_key_end().empty()) {
    // partition_start_key < partition.end OR tablet does not end.
    return result;
  }

  return nullptr;
}

boost::optional<std::vector<RemoteTabletPtr>> MetaCache::FastLookupAllTabletsUnlocked(
    const std::shared_ptr<const YBTable>& table) {
  auto tablets = std::vector<RemoteTabletPtr>();
  auto it = tables_.find(table->id());
  if (PREDICT_FALSE(it == tables_.end())) {
    // No cache available for this table.
    return boost::none;
  }

  for (const auto& tablet : it->second.all_tablets) {
    if (tablet->stale()) {
      return boost::none;
    }
    tablets.push_back(tablet);
  }

  if (tablets.empty()) {
    return boost::none;
  }
  return tablets;
}

// We disable thread safety analysis in this function due to manual conditional locking.
RemoteTabletPtr MetaCache::FastLookupTabletByKeyUnlocked(
    const TableId& table_id, const VersionedPartitionStartKey& partition_start) {
  // Fast path: lookup in the cache.
  auto result = LookupTabletByKeyFastPathUnlocked(table_id, partition_start);
  if (result && result->HasLeader()) {
    VLOG_WITH_PREFIX(5) << "Fast lookup: found tablet " << result->tablet_id();
    return result;
  }

  return nullptr;
}

template <class Mutex>
bool IsUniqueLock(const std::lock_guard<Mutex>*) {
  return true;
}

template <class Mutex>
bool IsUniqueLock(const SharedLock<Mutex>*) {
  return false;
}

// partition_group_start should be not nullptr and points to PartitionGroupStartKeyPtr that will be
// initialized only if it is nullptr. This is an optimization to avoid recalculation of
// partition_group_start in subsequent call of this function (see MetaCache::LookupTabletByKey).
template <class Lock>
bool MetaCache::DoLookupTabletByKey(
    const std::shared_ptr<const YBTable>& table, const VersionedTablePartitionListPtr& partitions,
    const PartitionKeyPtr& partition_start, CoarseTimePoint deadline,
    LookupTabletCallback* callback, PartitionGroupStartKeyPtr* partition_group_start) {
  DCHECK_ONLY_NOTNULL(partition_group_start);
  RemoteTabletPtr tablet;
  Status status = Status::OK();
  auto scope_exit = ScopeExit([callback, &tablet, &status] {
    if (tablet) {
      (*callback)(tablet);
    } else if (!status.ok()) {
      (*callback)(status);
    }
  });
  int64_t request_no;
  {
    Lock lock(mutex_);
    tablet = FastLookupTabletByKeyUnlocked(table->id(), {partition_start, partitions->version});
    if (tablet) {
      return true;
    }

    auto table_it = tables_.find(table->id());
    TableData* table_data;
    if (table_it == tables_.end()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << Format(
          "missed table_id $0", table->id());
      if (!IsUniqueLock(&lock)) {
        return false;
      }
      table_it = InitTableDataUnlocked(table->id(), partitions);
    }
    table_data = &table_it->second;

    if (table_data->partition_list->version != partitions->version ||
        (PREDICT_FALSE(RandomActWithProbability(
            FLAGS_TEST_simulate_lookup_partition_list_mismatch_probability)) &&
         table->table_type() != YBTableType::TRANSACTION_STATUS_TABLE_TYPE)) {
      status = STATUS(
          TryAgain,
          Format(
              "MetaCache's table $0 partitions version does not match, cached: $1, got: $2, "
              "refresh required",
              table->ToString(), table_data->partition_list->version, partitions->version),
          ClientError(ClientErrorCode::kTablePartitionListIsStale));
      return true;
    }

    if (!*partition_group_start) {
      *partition_group_start = client::FindPartitionStart(
          partitions, *partition_start, kPartitionGroupSize);
    }

    auto& tablet_lookups_by_group = table_data->tablet_lookups_by_group;
    LookupDataGroup* lookups_group;
    {
      auto lookups_group_it = tablet_lookups_by_group.find(**partition_group_start);
      if (lookups_group_it == tablet_lookups_by_group.end()) {
        if (!IsUniqueLock(&lock)) {
          return false;
        }
        lookups_group = &tablet_lookups_by_group[**partition_group_start];
      } else {
        lookups_group = &lookups_group_it->second;
      }
    }
    lookups_group->lookups.Push(new LookupData(*callback, deadline, partition_start));
    request_no = lookup_serial_.fetch_add(1, std::memory_order_acq_rel);
    int64_t expected = 0;
    if (!lookups_group->running_request_number.compare_exchange_strong(
            expected, request_no, std::memory_order_acq_rel)) {
      VLOG_WITH_PREFIX_AND_FUNC(5)
          << "Lookup is already running for table: " << table->ToString()
          << ", partition_group_start: " << Slice(**partition_group_start).ToDebugHexString()
          << ", partition_list_version: " << partitions->version
          << ", request_no: " << expected;
      return true;
    }
  }

  auto rpc = std::make_shared<LookupByKeyRpc>(
      this, table, VersionedPartitionGroupStartKey{*partition_group_start, partitions->version},
      request_no, deadline);
  VLOG_WITH_PREFIX_AND_FUNC(4)
      << "Started lookup for table: " << table->ToString()
      << ", partition_group_start: " << Slice(**partition_group_start).ToDebugHexString()
      << ", rpc: " << AsString(rpc);
  client_->data_->rpcs_.RegisterAndStart(rpc, rpc->RpcHandle());
  return true;
}

template <class Lock>
bool MetaCache::DoLookupAllTablets(const std::shared_ptr<const YBTable>& table,
                                   CoarseTimePoint deadline,
                                   LookupTabletRangeCallback* callback) {
  VLOG_WITH_PREFIX(3) << "DoLookupAllTablets() for table: " << table->ToString();
  int64_t request_no;
  {
    Lock lock(mutex_);
    if (PREDICT_TRUE(!FLAGS_TEST_force_master_lookup_all_tablets)) {
      auto tablets = FastLookupAllTabletsUnlocked(table);
      if (tablets.has_value()) {
        VLOG_WITH_PREFIX(4) << "tablets has value";
        (*callback)(*tablets);
        return true;
      }
    }

    if (!IsUniqueLock(&lock)) {
      return false;
    }
    auto table_it = tables_.find(table->id());
    if (table_it == tables_.end()) {
      table_it = InitTableDataUnlocked(table->id(), table->GetVersionedPartitions());
    }
    auto& table_data = table_it->second;

    auto& full_table_lookups = table_data.full_table_lookups;
    full_table_lookups.lookups.Push(new LookupData(*callback, deadline, nullptr));
    request_no = lookup_serial_.fetch_add(1, std::memory_order_acq_rel);
    int64_t expected = 0;
    if (!full_table_lookups.running_request_number.compare_exchange_strong(
        expected, request_no, std::memory_order_acq_rel)) {
      VLOG_WITH_PREFIX_AND_FUNC(5)
          << "Lookup is already running for table: " << table->ToString();
      return true;
    }
  }

  VLOG_WITH_PREFIX_AND_FUNC(4)
      << "Start lookup for table: " << table->ToString();

  auto rpc = std::make_shared<LookupFullTableRpc>(this, table, request_no, deadline);
  client_->data_->rpcs_.RegisterAndStart(rpc, rpc->RpcHandle());
  return true;
}

// We disable thread safety analysis in this function due to manual conditional locking.
void MetaCache::LookupTabletByKey(const std::shared_ptr<YBTable>& table,
                                  const PartitionKey& partition_key,
                                  CoarseTimePoint deadline,
                                  LookupTabletCallback callback,
                                  FailOnPartitionListRefreshed fail_on_partition_list_refreshed) {
  const auto now = CoarseMonoClock::Now();
  if (deadline < now) {
    callback(STATUS_FORMAT(
        TimedOut, "LookupTabletByKey attempted after deadline expired, passed since deadline: $0",
        now - deadline));
    return;
  }

  if (FLAGS_TEST_sleep_before_metacache_lookup_ms > 0) {
    MonoDelta sleep_time = MonoDelta::FromMilliseconds(1) *
                           RandomUniformInt(1, FLAGS_TEST_sleep_before_metacache_lookup_ms);
    SleepFor(sleep_time);
    VLOG_WITH_FUNC(2) << "Slept for " << sleep_time;
  }
  if (table->ArePartitionsStale()) {
    RefreshTablePartitions(
        table,
        [this, table, partition_key, deadline, callback = std::move(callback),
         fail_on_partition_list_refreshed](const auto& status) {
          if (!status.ok()) {
            callback(status);
            return;
          }
          if (fail_on_partition_list_refreshed) {
            callback(STATUS_EC_FORMAT(
                TryAgain, ClientError(ClientErrorCode::kTablePartitionListRefreshed),
                "Partition list for table $0 has been refreshed.", table->id()));
            return;
          }
          LookupTabletByKey(
              table, partition_key, deadline, std::move(callback),
              fail_on_partition_list_refreshed);
        });
    return;
  }

  const auto table_partition_list = table->GetVersionedPartitions();
  const auto partition_start = client::FindPartitionStart(table_partition_list, partition_key);
  VLOG_WITH_PREFIX_AND_FUNC(5) << "Table: " << table->ToString()
                    << ", table_partition_list: " << table_partition_list->ToString()
                    << ", partition_key: " << Slice(partition_key).ToDebugHexString()
                    << ", partition_start: " << Slice(*partition_start).ToDebugHexString();

  PartitionGroupStartKeyPtr partition_group_start;
  if (DoLookupTabletByKey<SharedLock<std::shared_timed_mutex>>(
          table, table_partition_list, partition_start, deadline, &callback,
          &partition_group_start)) {
    return;
  }

  bool result = DoLookupTabletByKey<std::lock_guard<std::shared_timed_mutex>>(
      table, table_partition_list, partition_start, deadline, &callback, &partition_group_start);
  LOG_IF(DFATAL, !result)
      << "Lookup was not started for table " << table->ToString()
      << ", partition_key: " << Slice(partition_key).ToDebugHexString();
}

void MetaCache::LookupAllTablets(const std::shared_ptr<YBTable>& table,
                                 CoarseTimePoint deadline,
                                 LookupTabletRangeCallback callback) {
  if (table->ArePartitionsStale()) {
    RefreshTablePartitions(
        table,
        [this, table, deadline, callback = std::move(callback)](const auto& status) {
          if (!status.ok()) {
            callback(status);
            return;
          }
          LookupAllTablets(table, deadline, std::move(callback));
        });
    return;
  }

  // We first want to check the cache in read-only mode, and only if we can't find anything
  // do a lookup in write mode.
  if (DoLookupAllTablets<SharedLock<std::shared_timed_mutex>>(table, deadline, &callback)) {
    return;
  }

  bool result = DoLookupAllTablets<std::lock_guard<std::shared_timed_mutex>>(
      table, deadline, &callback);
  LOG_IF(DFATAL, !result)
      << "Full table lookup was not started for table " << table->ToString();
}

std::optional<RemoteTabletPtr> MetaCache::LookupTabletByIdFastPathUnlocked(
    const TabletId& tablet_id) {
  auto it = tablets_by_id_.find(tablet_id);
  if (it != tablets_by_id_.end()) {
    return it->second;
  }
  if (deleted_tablets_.contains(tablet_id)) {
    return nullptr;
  }
  return std::nullopt;
}

template <class Lock>
bool MetaCache::DoLookupTabletById(
    const TabletId& tablet_id,
    const std::shared_ptr<const YBTable>& table,
    master::IncludeInactive include_inactive,
    master::IncludeDeleted include_deleted,
    CoarseTimePoint deadline,
    UseCache use_cache,
    LookupTabletCallback* callback) {
  std::optional<RemoteTabletPtr> tablet = std::nullopt;
  Status status = Status::OK();
  auto scope_exit = ScopeExit([callback, &tablet, &status] {
    if (tablet) {
      (*callback)(*tablet);
    } else if (!status.ok()) {
      (*callback)(status);
    }
  });
  int64_t request_no;
  int64_t lookups_without_new_replicas = 0;
  {
    Lock lock(mutex_);

    // Fast path: lookup in the cache.
    tablet = LookupTabletByIdFastPathUnlocked(tablet_id);
    if (tablet) {
      if (!*tablet) {
        VLOG_WITH_PREFIX(5) << "Fast lookup: tablet deleted";
        if (use_cache) {
          if (!include_deleted) {
            tablet = std::nullopt;
            status = STATUS(NotFound, "Tablet deleted");
          }
          return true;
        }
      } else {
        VLOG_WITH_PREFIX(5) << "Fast lookup: candidate tablet " << AsString(*tablet);
        if (use_cache && (*tablet)->HasLeader()) {
          // tablet->HasLeader() check makes MetaCache send RPC to master in case of no tablet with
          // tablet_id is found on all replicas.
          VLOG_WITH_PREFIX(5) << "Fast lookup: found tablet " << (*tablet)->tablet_id();
          return true;
        }
        lookups_without_new_replicas = (*tablet)->lookups_without_new_replicas();
      }

      tablet = std::nullopt;
    }

    LookupDataGroup* lookup;
    {
      auto lookup_it = tablet_lookups_by_id_.find(tablet_id);
      if (lookup_it == tablet_lookups_by_id_.end()) {
        if (!IsUniqueLock(&lock)) {
          return false;
        }
        lookup = &tablet_lookups_by_id_[tablet_id];
      } else {
        lookup = &lookup_it->second;
      }
    }
    lookup->lookups.Push(new LookupData(*callback, deadline, nullptr));
    request_no = lookup_serial_.fetch_add(1, std::memory_order_acq_rel);
    int64_t expected = 0;
    if (!lookup->running_request_number.compare_exchange_strong(
            expected, request_no, std::memory_order_acq_rel)) {
      VLOG_WITH_PREFIX_AND_FUNC(5) << "Lookup already running for tablet: " << tablet_id;
      return true;
    }
  }

  VLOG_WITH_PREFIX_AND_FUNC(4) << "Start lookup for tablet " << tablet_id << ": " << request_no;

  auto rpc = std::make_shared<LookupByIdRpc>(
      this, tablet_id, table, include_inactive, include_deleted, request_no, deadline,
      lookups_without_new_replicas);
  client_->data_->rpcs_.RegisterAndStart(rpc, rpc->RpcHandle());
  return true;
}

void MetaCache::LookupTabletById(const TabletId& tablet_id,
                                 const std::shared_ptr<const YBTable>& table,
                                 master::IncludeInactive include_inactive,
                                 master::IncludeDeleted include_deleted,
                                 CoarseTimePoint deadline,
                                 LookupTabletCallback callback,
                                 UseCache use_cache) {
  VLOG_WITH_PREFIX_AND_FUNC(5) << "(" << tablet_id << ", " << use_cache << ")";

  if (DoLookupTabletById<SharedLock<decltype(mutex_)>>(
          tablet_id, table, include_inactive, include_deleted, deadline, use_cache, &callback)) {
    return;
  }

  auto result = DoLookupTabletById<std::lock_guard<decltype(mutex_)>>(
      tablet_id, table, include_inactive, include_deleted, deadline, use_cache, &callback);
  LOG_IF(DFATAL, !result) << "Lookup was not started for tablet " << tablet_id;
}

void MetaCache::RefreshTablePartitions(
    const std::shared_ptr<YBTable>& table, StdStatusCallback callback) {
  table->RefreshPartitions(
      client_,
      [this, table, callback = std::move(callback)](
          const Status& status) {
        if (status.ok()) {
          InvalidateTableCache(*table);
        }
        callback(status);
  });
}

void MetaCache::MarkTSFailed(RemoteTabletServer* ts,
                             const Status& status) {
  LOG_WITH_PREFIX(INFO) << "Marking tablet server " << ts->ToString() << " as failed.";
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

std::future<Result<internal::RemoteTabletPtr>> MetaCache::LookupTabletByKeyFuture(
    const std::shared_ptr<YBTable>& table,
    const PartitionKey& partition_key,
    CoarseTimePoint deadline) {
  return MakeFuture<Result<internal::RemoteTabletPtr>>([&](auto callback) {
    this->LookupTabletByKey(table, partition_key, deadline, std::move(callback));
  });
}

LookupDataGroup::~LookupDataGroup() {
  std::vector<LookupData*> leftovers;
  while (auto* d = lookups.Pop()) {
    leftovers.push_back(d);
  }
  if (!leftovers.empty()) {
    LOG(DFATAL) << Format(
        "Destructing LookupDataGroup($0), running_request_number: $1 with non empty lookups: $2",
        static_cast<void*>(this), running_request_number, leftovers);
  }
}

void LookupDataGroup::Finished(
    int64_t request_no, const ToStringable& id, bool allow_absence) {
  int64_t expected = request_no;
  if (running_request_number.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) {
    max_completed_request_number = std::max(max_completed_request_number, request_no);
    VLOG_WITH_FUNC(2) << "Finished lookup for " << id.ToString() << ", no: " << request_no;
    return;
  }

  if ((expected == 0 && max_completed_request_number <= request_no) && !allow_absence) {
    LOG(DFATAL) << "Lookup was not running for " << id.ToString() << ", expected: " << request_no;
    return;
  }

  LOG(INFO)
      << "Finished lookup for " << id.ToString() << ": " << request_no << ", while "
      << expected << " was running, could happen during tablet split";
}

TableData::TableData(const VersionedTablePartitionListPtr& partition_list_)
    : partition_list(partition_list_) {
  DCHECK_ONLY_NOTNULL(partition_list);
}

std::string VersionedPartitionStartKey::ToString() const {
  return YB_STRUCT_TO_STRING(key, partition_list_version);
}

std::string RemoteReplica::ToString() const {
  return Format("$0 ($1, $2)",
                ts->permanent_uuid(),
                PeerRole_Name(role),
                Failed() ? "FAILED" : "OK");
}

} // namespace internal
} // namespace client
} // namespace yb
