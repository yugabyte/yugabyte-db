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

#include "yb/master/ts_descriptor.h"

#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/master/catalog_manager_util.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_util.h"

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/status_format.h"

DECLARE_uint32(master_ts_ysql_catalog_lease_ms);

DEFINE_RUNTIME_uint32(
    ysql_operation_lease_ttl_ms, 10 * 1000,
    "The lifetime of client operation lease extensions. The client operation lease allows tservers "
    "to host pg sessions and serve reads and writes to user data.");

DECLARE_int32(tserver_unresponsive_timeout_ms);

DECLARE_bool(TEST_enable_ysql_operation_lease);

namespace yb {
namespace master {

TSDescriptor::TSDescriptor(const std::string& permanent_uuid,
                           RegisteredThroughHeartbeat registered_through_heartbeat,
                           CloudInfoPB&& local_cloud_info,
                           rpc::ProxyCache* proxy_cache)
  : permanent_uuid_(permanent_uuid),
    local_cloud_info_(std::move(local_cloud_info)),
    proxy_cache_(proxy_cache),
    last_heartbeat_(registered_through_heartbeat ? MonoTime::Now() : MonoTime()),
    registered_through_heartbeat_(registered_through_heartbeat),
    latest_report_seqno_(std::numeric_limits<int32_t>::min()),
    has_tablet_report_(false),
    has_faulty_drive_(false),
    recent_replica_creations_(0),
    last_replica_creations_decay_(MonoTime::Now()),
    num_live_replicas_(0) {}

Result<std::pair<TSDescriptorPtr, TSDescriptor::WriteLock>> TSDescriptor::CreateNew(
    const NodeInstancePB& instance,
    const TSRegistrationPB& registration,
    CloudInfoPB&& local_cloud_info,
    rpc::ProxyCache* proxy_cache,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  auto desc = std::make_shared<TSDescriptor>(
      instance.permanent_uuid(), registered_through_heartbeat, std::move(local_cloud_info),
      proxy_cache);
  auto lock = VERIFY_RESULT(desc->UpdateRegistration(
      instance, registration, registered_through_heartbeat));
  return std::make_pair(std::move(desc), std::move(lock));
}

TSDescriptorPtr TSDescriptor::LoadFromEntry(
    const std::string& permanent_uuid, const SysTabletServerEntryPB& metadata,
    CloudInfoPB&& cloud_info, rpc::ProxyCache* proxy_cache, HybridTime now) {
  // The RegisteredThroughHeartbeat parameter controls how last_heartbeat_ is initialized.
  // If true, last_heartbeat_ is set to now. If false, last_heartbeat_ is an uninitialized MonoTime.
  // Use true here because:
  //   1. if the tserver is live, the only reasonable time to mark it as unresponsive is
  //      tserver_unresponsive_timeout_ms from now.
  //   2. if the tserver is unresponsive, this field doesn't matter.
  auto desc = std::make_shared<TSDescriptor>(
      permanent_uuid, RegisteredThroughHeartbeat::kTrue, std::move(cloud_info), proxy_cache);
  // todo(zdrudi): should give some thought to state here, in particular LIVE.
  // https://github.com/yugabyte/yugabyte-db/issues/24102
  desc->Load(metadata);
  std::lock_guard spinlock(desc->mutex_);
  desc->placement_id_ = generate_placement_id(metadata.registration().cloud_info());
  if (metadata.live_client_operation_lease()) {
    desc->client_operation_lease_deadline_ =
        now.AddMilliseconds(GetAtomicFlag(&FLAGS_ysql_operation_lease_ttl_ms));
  }
  return desc;
}

Result<TSDescriptor::WriteLock> TSDescriptor::UpdateRegistration(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  CHECK_EQ(instance.permanent_uuid(), permanent_uuid());

  auto l = LockForWrite();
  int64_t latest_seqno = l->pb.instance_seqno();
  if (instance.instance_seqno() < latest_seqno) {
    return STATUS(AlreadyPresent,
      Format("Cannot register with sequence number $0:"
                          " Already have a registration from sequence number $1",
                          instance.instance_seqno(),
                          latest_seqno));
  } else if (instance.instance_seqno() == latest_seqno) {
    // It's possible that the TS registered, but our response back to it
    // got lost, so it's trying to register again with the same sequence
    // number. That's fine.
    LOG(INFO) << "Processing retry of TS registration from " << instance.ShortDebugString();
  }

  std::lock_guard spinlock(mutex_);
  // After re-registering, make the TS re-report its tablets.
  has_tablet_report_ = false;
  l.mutable_data()->pb.set_instance_seqno(instance.instance_seqno());
  *l.mutable_data()->pb.mutable_registration() = registration.common();
  *l.mutable_data()->pb.mutable_resources() = registration.resources();
  l.mutable_data()->pb.set_state(
      registered_through_heartbeat ? SysTabletServerEntryPB::LIVE
                                   : SysTabletServerEntryPB::UNRESPONSIVE);
  latest_report_seqno_ = std::numeric_limits<int32_t>::min();
  placement_id_ = generate_placement_id(registration.common().cloud_info());
  proxies_.reset();
  // The new incarnation heartbeating does not have a live lease. If the previous incarnation did,
  // set live_client_operation_lease to false so UpdateFromHeartbeat will grant the new incarnation
  // a new lease.
  if (instance.instance_seqno() != latest_seqno && l->pb.live_client_operation_lease()) {
    // todo(zdrudi): kick off an async task to clear out all locks held by the previous incarnation.
    client_operation_lease_deadline_ = HybridTime();
    l.mutable_data()->pb.set_live_client_operation_lease(false);
  }
  return std::move(l);
}

std::string TSDescriptor::placement_uuid() const {
  return LockForRead()->pb.registration().placement_uuid();
}

std::string TSDescriptor::generate_placement_id(const CloudInfoPB& ci) {
  return Format(
      "$0:$1:$2", ci.placement_cloud(), ci.placement_region(), ci.placement_zone());
}

std::string TSDescriptor::placement_id() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return placement_id_;
}

Result<std::optional<ClientOperationLeaseUpdate>> TSDescriptor::UpdateFromHeartbeat(
    const TSHeartbeatRequestPB& req, const TSDescriptor::WriteLock& lock, HybridTime hybrid_time) {
  DCHECK_GE(req.num_live_tablets(), 0);
  DCHECK_GE(req.leader_count(), 0);
  std::optional<ClientOperationLeaseUpdate> lease_delta;
  {
    std::lock_guard l(mutex_);
    RETURN_NOT_OK(IsReportCurrentUnlocked(
        req.common().ts_instance(),
        req.has_tablet_report() ? std::optional(std::cref(req.tablet_report())) : std::nullopt,
        lock));
    last_heartbeat_ = MonoTime::Now();
    num_live_replicas_ = req.num_live_tablets();
    leader_count_ = req.leader_count();
    physical_time_ = req.ts_physical_time();
    hybrid_time_ = HybridTime::FromPB(req.ts_hybrid_time());
    heartbeat_rtt_ = MonoDelta::FromMicroseconds(req.rtt_us());
    if (req.has_tablet_report()) {
      latest_report_seqno_ =
          std::max(latest_report_seqno_, req.tablet_report().sequence_number());
    }
    if (req.has_faulty_drive()) {
      has_faulty_drive_ = req.faulty_drive();
    }
    if (GetAtomicFlag(&FLAGS_TEST_enable_ysql_operation_lease)) {
      lease_delta = ClientOperationLeaseUpdate();
      client_operation_lease_deadline_ =
          hybrid_time.AddMilliseconds(GetAtomicFlag(&FLAGS_ysql_operation_lease_ttl_ms));
      lease_delta->lease_deadline = client_operation_lease_deadline_;
    }
  }
  if (lock->pb.state() == SysTabletServerEntryPB::REMOVED) {
    return STATUS_FORMAT(
        IllegalState, "Processing ts heartbeat for ts $0 raced with removing the ts", id());
  }
  if (lock->pb.state() != SysTabletServerEntryPB::LIVE) {
    lock.mutable_data()->pb.set_state(SysTabletServerEntryPB::LIVE);
  }
  // If this heartbeat included registration data with a later instance_seqno, the registration
  // code signals we should grant a new lease to this tserver instance by setting
  // live_client_operation_lease to false on the write copy of the protobuf.
  // So we check both read and write copies here.
  if (lease_delta && (!lock->pb.live_client_operation_lease() ||
                      !lock.mutable_data()->pb.live_client_operation_lease())) {
    lock.mutable_data()->pb.set_live_client_operation_lease(true);
    uint64_t new_lease_epoch = lock->pb.lease_epoch() + 1;
    lock.mutable_data()->pb.set_lease_epoch(new_lease_epoch);
    lease_delta->new_lease = true;
    lease_delta->lease_epoch = new_lease_epoch;
  }
  return lease_delta;
}

MonoDelta TSDescriptor::TimeSinceHeartbeat() const {
  auto last_heartbeat = LastHeartbeatTime();
  return MonoTime::Now().GetDeltaSince(last_heartbeat ? last_heartbeat : MonoTime::kMin);
}

MonoTime TSDescriptor::LastHeartbeatTime() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return last_heartbeat_;
}

int64_t TSDescriptor::latest_seqno() const {
  return LockForRead()->pb.instance_seqno();
}

int32_t TSDescriptor::latest_report_seqno() const {
    SharedLock<decltype(mutex_)> l(mutex_);
    return latest_report_seqno_;
}

bool TSDescriptor::has_tablet_report() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return has_tablet_report_;
}

void TSDescriptor::set_has_tablet_report(bool has_report) {
  std::lock_guard l(mutex_);
  has_tablet_report_ = has_report;
}

bool TSDescriptor::has_faulty_drive() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return has_faulty_drive_;
}

bool TSDescriptor::registered_through_heartbeat() const { return registered_through_heartbeat_; }

void TSDescriptor::DecayRecentReplicaCreationsUnlocked() {
  // In most cases, we won't have any recent replica creations, so
  // we don't need to bother calling the clock, etc.
  if (recent_replica_creations_ == 0) return;

  const double kHalflifeSecs = 60;
  MonoTime now = MonoTime::Now();
  double secs_since_last_decay = now.GetDeltaSince(last_replica_creations_decay_).ToSeconds();
  recent_replica_creations_ *= pow(0.5, secs_since_last_decay / kHalflifeSecs);

  // If sufficiently small, reset down to 0 to take advantage of the fast path above.
  if (recent_replica_creations_ < 1e-12) {
    recent_replica_creations_ = 0;
  }
  last_replica_creations_decay_ = now;
}

void TSDescriptor::IncrementRecentReplicaCreations() {
  std::lock_guard l(mutex_);
  DecayRecentReplicaCreationsUnlocked();
  recent_replica_creations_ += 1;
}

double TSDescriptor::RecentReplicaCreations() {
  std::lock_guard l(mutex_);
  DecayRecentReplicaCreationsUnlocked();
  return recent_replica_creations_;
}

ServerRegistrationPB TSDescriptor::GetRegistration() const {
  return LockForRead()->pb.registration();
}

ResourcesPB TSDescriptor::GetResources() const {
  return LockForRead()->pb.resources();
}

TSInformationPB TSDescriptor::GetTSInformationPB() const {
  auto l = LockForRead();
  TSInformationPB ts_info_pb;
  *ts_info_pb.mutable_registration()->mutable_common() = l->pb.registration();
  *ts_info_pb.mutable_registration()->mutable_resources() = l->pb.resources();
  ts_info_pb.mutable_tserver_instance()->set_permanent_uuid(permanent_uuid());
  ts_info_pb.mutable_tserver_instance()->set_instance_seqno(l->pb.instance_seqno());
  return ts_info_pb;
}

TSRegistrationPB TSDescriptor::GetTSRegistrationPB() const {
  auto l = LockForRead();
  TSRegistrationPB ts_reg;
  *ts_reg.mutable_common() = l->pb.registration();
  *ts_reg.mutable_resources() = l->pb.resources();
  return ts_reg;
}

NodeInstancePB TSDescriptor::GetNodeInstancePB() const {
  NodeInstancePB node;
  node.set_permanent_uuid(permanent_uuid());
  node.set_instance_seqno(LockForRead()->pb.instance_seqno());
  return node;
}

bool TSDescriptor::MatchesCloudInfo(const CloudInfoPB& cloud_info) const {
  return CatalogManagerUtil::IsCloudInfoPrefix(
      cloud_info, LockForRead()->pb.registration().cloud_info());
}

CloudInfoPB TSDescriptor::GetCloudInfo() const {
  return LockForRead()->pb.registration().cloud_info();
}

bool TSDescriptor::IsBlacklisted(const BlacklistSet& blacklist) const {
  return yb::master::IsBlacklisted(LockForRead()->pb.registration(), blacklist);
}

bool TSDescriptor::IsRunningOn(const HostPortPB& hp) const {
  return yb::master::IsRunningOn(LockForRead()->pb.registration(), hp);
}

Result<HostPort> TSDescriptor::GetHostPort() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return GetHostPortUnlocked();
}

Result<HostPort> TSDescriptor::GetHostPortUnlocked() const {
  auto l = LockForRead();
  const auto& addr = DesiredHostPort(l->pb.registration(), local_cloud_info_);
  if (addr.host().empty()) {
    return STATUS_FORMAT(NetworkError, "Unable to find the TS address for $0: $1",
                         permanent_uuid(), l->pb.registration().ShortDebugString());
  }

  return HostPortFromPB(addr);
}

bool TSDescriptor::IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const {
  if (IsReadOnlyTS(replication_info)) {
    // Read-only ts are not voting and therefore cannot be leaders.
    return false;
  }

  if (replication_info.affinitized_leaders_size() == 0 &&
      replication_info.multi_affinitized_leaders_size() == 0) {
    // If there are no affinitized leaders, all ts can be leaders.
    return true;
  }

  for (const auto& zone_set : replication_info.multi_affinitized_leaders()) {
    for (const CloudInfoPB& cloud_info : zone_set.zones()) {
      if (MatchesCloudInfo(cloud_info)) {
        return true;
      }
    }
  }

  // Handle old un-updated config if any
  for (const CloudInfoPB& cloud_info : replication_info.affinitized_leaders()) {
    if (MatchesCloudInfo(cloud_info)) {
      return true;
    }
  }
  return false;
}

void TSDescriptor::UpdateMetrics(const TServerMetricsPB& metrics) {
  std::lock_guard l(mutex_);
  ts_metrics_.total_memory_usage = metrics.total_ram_usage();
  ts_metrics_.total_sst_file_size = metrics.total_sst_file_size();
  ts_metrics_.uncompressed_sst_file_size = metrics.uncompressed_sst_file_size();
  ts_metrics_.num_sst_files = metrics.num_sst_files();
  ts_metrics_.read_ops_per_sec = metrics.read_ops_per_sec();
  ts_metrics_.write_ops_per_sec = metrics.write_ops_per_sec();
  ts_metrics_.uptime_seconds = metrics.uptime_seconds();
  ts_metrics_.path_metrics.clear();
  for (const auto& path_metric : metrics.path_metrics()) {
    ts_metrics_.path_metrics[path_metric.path_id()] =
        { path_metric.used_space(), path_metric.total_space() };
  }
  ts_metrics_.disable_tablet_split_if_default_ttl = metrics.disable_tablet_split_if_default_ttl();
}

void TSDescriptor::GetMetrics(TServerMetricsPB* metrics) {
  CHECK(metrics);
  SharedLock<decltype(mutex_)> l(mutex_);
  metrics->set_total_ram_usage(ts_metrics_.total_memory_usage);
  metrics->set_total_sst_file_size(ts_metrics_.total_sst_file_size);
  metrics->set_uncompressed_sst_file_size(ts_metrics_.uncompressed_sst_file_size);
  metrics->set_num_sst_files(ts_metrics_.num_sst_files);
  metrics->set_read_ops_per_sec(ts_metrics_.read_ops_per_sec);
  metrics->set_write_ops_per_sec(ts_metrics_.write_ops_per_sec);
  metrics->set_uptime_seconds(ts_metrics_.uptime_seconds);
  for (const auto& path_metric : ts_metrics_.path_metrics) {
    auto* new_path_metric = metrics->add_path_metrics();
    new_path_metric->set_path_id(path_metric.first);
    new_path_metric->set_used_space(path_metric.second.used_space);
    new_path_metric->set_total_space(path_metric.second.total_space);
  }
  metrics->set_disable_tablet_split_if_default_ttl(ts_metrics_.disable_tablet_split_if_default_ttl);
}

Status TSDescriptor::IsReportCurrent(
    const NodeInstancePB& ts_instance, const TabletReportPB& report) {
  auto cow_lock = LockForRead();
  SharedLock<decltype(mutex_)> l(mutex_);
  return IsReportCurrentUnlocked(ts_instance, std::cref(report), cow_lock);
}

template <typename LockType>
Status TSDescriptor::IsReportCurrentUnlocked(
    const NodeInstancePB& ts_instance,
    std::optional<std::reference_wrapper<const TabletReportPB>> report, const LockType& l) {
  // Check instance seqno: did this tserver restart and send us another tablet report before we
  // finished with this one?
  if (l->pb.instance_seqno() != ts_instance.instance_seqno()) {
    return STATUS_FORMAT(
        IllegalState,
        "Stale tablet report for ts $0: instance sequence number in tablet report is $1 but "
        "current sequence number is $2",
        permanent_uuid(), ts_instance.instance_seqno(), l->pb.instance_seqno());
  }
  // Check report sequence number: Has the client tserver timed out on the heartbeat RPC carrying
  // this tablet report and already sent another one?
  if (report && report->get().sequence_number() < latest_report_seqno_) {
    return STATUS_FORMAT(
        IllegalState,
        "Stale tablet report for ts $0: latest tablet report sequence number for this tserver is "
        "$1, but still processing a tablet report with sequence number $2",
        permanent_uuid(), latest_report_seqno_, report->get().sequence_number());
  }
  return Status::OK();
}

bool TSDescriptor::HasTabletDeletePending() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return !tablets_pending_delete_.empty();
}

void TSDescriptor::AddPendingTabletDelete(const std::string& tablet_id) {
  std::lock_guard l(mutex_);
  tablets_pending_delete_.insert(tablet_id);
}

size_t TSDescriptor::ClearPendingTabletDelete(const std::string& tablet_id) {
  std::lock_guard l(mutex_);
  return tablets_pending_delete_.erase(tablet_id);
}

std::string TSDescriptor::PendingTabletDeleteToString() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return yb::ToString(tablets_pending_delete_);
}

std::set<std::string> TSDescriptor::TabletsPendingDeletion() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return tablets_pending_delete_;
}

std::size_t TSDescriptor::NumTasks() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return tablets_pending_delete_.size();
}

bool TSDescriptor::IsLive() const {
  return LockForRead()->pb.state() == SysTabletServerEntryPB::LIVE;
}

bool TSDescriptor::IsLiveAndHasReported() const {
  return IsLive() && has_tablet_report();
}

bool TSDescriptor::HasYsqlCatalogLease() const {
  return TimeSinceHeartbeat().ToMilliseconds() <
         GetAtomicFlag(&FLAGS_master_ts_ysql_catalog_lease_ms) && !IsReplaced();
}

std::string TSDescriptor::ToString() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return Format("{ permanent_uuid: $0 registration: $1 placement_id: $2 }",
                permanent_uuid(), LockForRead()->pb.registration(), placement_id_);
}

bool TSDescriptor::IsReadOnlyTS(const ReplicationInfoPB& replication_info) const {
  const PlacementInfoPB& placement_info = replication_info.live_replicas();
  if (placement_info.has_placement_uuid()) {
    return placement_info.placement_uuid() != placement_uuid();
  }
  return !placement_uuid().empty();
}

std::pair<std::optional<TSDescriptor::WriteLock>, std::optional<uint64_t>>
TSDescriptor::MaybeUpdateLiveness(MonoTime mono_time, HybridTime hybrid_time) {
  auto proto_lock = LockForWrite();
  bool updated = false;
  SharedLock<decltype(mutex_)> transient_lock(mutex_);
  std::optional<uint64_t> expired_lease_epoch;
  if (proto_lock->pb.state() == SysTabletServerEntryPB::LIVE && last_heartbeat_ &&
      mono_time.GetDeltaSince(last_heartbeat_).ToMilliseconds() >
          GetAtomicFlag(&FLAGS_tserver_unresponsive_timeout_ms)) {
    proto_lock.mutable_data()->pb.set_state(SysTabletServerEntryPB::UNRESPONSIVE);
    updated = true;
  }
  if (GetAtomicFlag(&FLAGS_TEST_enable_ysql_operation_lease)) {
    if (proto_lock->pb.live_client_operation_lease() &&
        client_operation_lease_deadline_ < hybrid_time) {
      proto_lock.mutable_data()->pb.set_live_client_operation_lease(false);
      updated = true;
      expired_lease_epoch = proto_lock->pb.lease_epoch();
    }
  }
  if (updated) {
    return std::make_pair(std::move(proto_lock), expired_lease_epoch);
  }
  return std::make_pair(std::nullopt, expired_lease_epoch);
}

bool TSDescriptor::HasLiveClientOperationLease() const {
  return LockForRead()->pb.live_client_operation_lease();
}

ClientOperationLeaseUpdatePB ClientOperationLeaseUpdate::ToPB() {
  ClientOperationLeaseUpdatePB pb;
  pb.set_lease_deadline_ht(lease_deadline.ToUint64());
  pb.set_new_lease(new_lease);
  pb.set_lease_epoch(lease_epoch);
  return pb;
}

} // namespace master
} // namespace yb
