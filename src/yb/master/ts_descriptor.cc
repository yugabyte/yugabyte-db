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

#include <math.h>

#include <mutex>
#include <vector>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.pb.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/net/net_util.h"

namespace yb {
namespace master {

Result<std::unique_ptr<TSDescriptor>> TSDescriptor::RegisterNew(
    const NodeInstancePB& instance,
    const TSRegistrationPB& registration,
    CloudInfoPB local_cloud_info,
    rpc::ProxyCache* proxy_cache) {
  std::unique_ptr<TSDescriptor> result = std::make_unique<YB_EDITION_NS_PREFIX TSDescriptor>(
      instance.permanent_uuid());
  RETURN_NOT_OK(result->Register(instance, registration, std::move(local_cloud_info), proxy_cache));
  return std::move(result);
}

TSDescriptor::TSDescriptor(std::string perm_id)
    : permanent_uuid_(std::move(perm_id)),
      latest_seqno_(-1),
      last_heartbeat_(MonoTime::Now()),
      has_tablet_report_(false),
      recent_replica_creations_(0),
      last_replica_creations_decay_(MonoTime::Now()),
      num_live_replicas_(0) {
}

TSDescriptor::~TSDescriptor() {
}

Status TSDescriptor::Register(const NodeInstancePB& instance,
                              const TSRegistrationPB& registration,
                              CloudInfoPB local_cloud_info,
                              rpc::ProxyCache* proxy_cache) {
  std::lock_guard<simple_spinlock> l(lock_);
  return RegisterUnlocked(instance, registration, std::move(local_cloud_info), proxy_cache);
}

Status TSDescriptor::RegisterUnlocked(
    const NodeInstancePB& instance,
    const TSRegistrationPB& registration,
    CloudInfoPB local_cloud_info,
    rpc::ProxyCache* proxy_cache) {
  CHECK_EQ(instance.permanent_uuid(), permanent_uuid_);

  if (instance.instance_seqno() < latest_seqno_) {
    return STATUS(AlreadyPresent,
      strings::Substitute("Cannot register with sequence number $0:"
                          " Already have a registration from sequence number $1",
                          instance.instance_seqno(),
                          latest_seqno_));
  } else if (instance.instance_seqno() == latest_seqno_) {
    // It's possible that the TS registered, but our response back to it
    // got lost, so it's trying to register again with the same sequence
    // number. That's fine.
    LOG(INFO) << "Processing retry of TS registration from " << instance.ShortDebugString();
  }

  latest_seqno_ = instance.instance_seqno();
  // After re-registering, make the TS re-report its tablets.
  has_tablet_report_ = false;

  registration_.reset(new TSRegistrationPB(registration));
  placement_id_ = generate_placement_id(registration.common().cloud_info());

  proxies_.reset();

  placement_uuid_ = "";
  if (registration.common().has_placement_uuid()) {
    placement_uuid_ = registration.common().placement_uuid();
  }
  local_cloud_info_ = std::move(local_cloud_info);
  proxy_cache_ = proxy_cache;

  return Status::OK();
}

std::string TSDescriptor::placement_uuid() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return placement_uuid_;
}

std::string TSDescriptor::generate_placement_id(const CloudInfoPB& ci) {
  return strings::Substitute(
      "$0:$1:$2", ci.placement_cloud(), ci.placement_region(), ci.placement_zone());
}

std::string TSDescriptor::placement_id() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return placement_id_;
}

void TSDescriptor::UpdateHeartbeatTime() {
  std::lock_guard<simple_spinlock> l(lock_);
  last_heartbeat_ = MonoTime::Now();
}

MonoDelta TSDescriptor::TimeSinceHeartbeat() const {
  MonoTime now(MonoTime::Now());
  std::lock_guard<simple_spinlock> l(lock_);
  return now.GetDeltaSince(last_heartbeat_);
}

int64_t TSDescriptor::latest_seqno() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return latest_seqno_;
}

bool TSDescriptor::has_tablet_report() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return has_tablet_report_;
}

void TSDescriptor::set_has_tablet_report(bool has_report) {
  std::lock_guard<simple_spinlock> l(lock_);
  has_tablet_report_ = has_report;
}

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
  std::lock_guard<simple_spinlock> l(lock_);
  DecayRecentReplicaCreationsUnlocked();
  recent_replica_creations_ += 1;
}

double TSDescriptor::RecentReplicaCreations() {
  std::lock_guard<simple_spinlock> l(lock_);
  DecayRecentReplicaCreationsUnlocked();
  return recent_replica_creations_;
}

void TSDescriptor::GetRegistration(TSRegistrationPB* reg) const {
  std::lock_guard<simple_spinlock> l(lock_);
  CHECK(registration_) << "No registration";
  CHECK_NOTNULL(reg)->CopyFrom(*registration_);
}

void TSDescriptor::GetTSInformationPB(TSInformationPB* ts_info) const {
  GetRegistration(ts_info->mutable_registration());
  GetNodeInstancePB(ts_info->mutable_tserver_instance());
}

bool TSDescriptor::MatchesCloudInfo(const CloudInfoPB& cloud_info) const {
  std::lock_guard<simple_spinlock> l(lock_);
  const auto& ci = registration_->common().cloud_info();

  return cloud_info.placement_cloud() == ci.placement_cloud() &&
         cloud_info.placement_region() == ci.placement_region() &&
         cloud_info.placement_zone() == ci.placement_zone();
}

bool TSDescriptor::IsRunningOn(const HostPortPB& hp) const {
  TSRegistrationPB reg;
  GetRegistration(&reg);
  auto predicate = [&hp](const HostPortPB& rhs) {
    return rhs.host() == hp.host() && rhs.port() == hp.port();
  };
  if (std::find_if(reg.common().private_rpc_addresses().begin(),
                   reg.common().private_rpc_addresses().end(),
                   predicate) != reg.common().private_rpc_addresses().end()) {
    return true;
  }
  if (std::find_if(reg.common().broadcast_addresses().begin(),
                   reg.common().broadcast_addresses().end(),
                   predicate) != reg.common().broadcast_addresses().end()) {
    return true;
  }
  return false;
}

void TSDescriptor::GetNodeInstancePB(NodeInstancePB* instance_pb) const {
  std::lock_guard<simple_spinlock> l(lock_);
  instance_pb->set_permanent_uuid(permanent_uuid_);
  instance_pb->set_instance_seqno(latest_seqno_);
}

Result<HostPort> TSDescriptor::GetHostPortUnlocked() const {
  const auto& addr = DesiredHostPort(registration_->common(), local_cloud_info_);
  if (addr.host().empty()) {
    return STATUS(NetworkError, "Unable to find the TS address: ", registration_->DebugString());
  }

  return HostPortFromPB(addr);
}

bool TSDescriptor::IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const {
  return true;
}

void TSDescriptor::UpdateMetrics(const TServerMetricsPB& metrics) {
  std::lock_guard<simple_spinlock> l(lock_);
  tsMetrics_.total_memory_usage = metrics.total_ram_usage();
  tsMetrics_.total_sst_file_size = metrics.total_sst_file_size();
  tsMetrics_.uncompressed_sst_file_size = metrics.uncompressed_sst_file_size();
  tsMetrics_.read_ops_per_sec = metrics.read_ops_per_sec();
  tsMetrics_.write_ops_per_sec = metrics.write_ops_per_sec();
  tsMetrics_.uptime_seconds = metrics.uptime_seconds();
}

bool TSDescriptor::HasTabletDeletePending() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !tablets_pending_delete_.empty();
}

bool TSDescriptor::IsTabletDeletePending(const std::string& tablet_id) const {
  std::lock_guard<simple_spinlock> l(lock_);
  return tablets_pending_delete_.count(tablet_id);
}

void TSDescriptor::AddPendingTabletDelete(const std::string& tablet_id) {
  std::lock_guard<simple_spinlock> l(lock_);
  tablets_pending_delete_.insert(tablet_id);
}

void TSDescriptor::ClearPendingTabletDelete(const std::string& tablet_id) {
  std::lock_guard<simple_spinlock> l(lock_);
  tablets_pending_delete_.erase(tablet_id);
}

std::string TSDescriptor::ToString() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return Format("{ permanent_uuid: $0 registration: $1 placement_id: $2 }",
                permanent_uuid_, registration_, placement_id_);
}

} // namespace master
} // namespace yb
