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

#include "yb/master/ts_manager.h"

#include <mutex>
#include <vector>

#include "yb/common/wire_protocol.h"

#include "yb/gutil/map-util.h"

#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/ts_descriptor.h"

#include "yb/util/atomic.h"

using std::string;

DEFINE_NON_RUNTIME_bool(
    master_register_ts_check_desired_host_port, true,
    "When set to true, master will only do duplicate address checks on the used host/port instead "
    "of on all. The used host/port combination depends on the value of --use_private_ip.");
TAG_FLAG(master_register_ts_check_desired_host_port, advanced);

DECLARE_int32(tserver_unresponsive_timeout_ms);

namespace yb {
namespace master {

namespace {
bool HasSameHostPort(const HostPortPB& lhs, const HostPortPB& rhs) {
  return lhs.host() == rhs.host() && lhs.port() == rhs.port();
}

bool HasSameHostPort(const google::protobuf::RepeatedPtrField<HostPortPB>& lhs,
                     const google::protobuf::RepeatedPtrField<HostPortPB>& rhs) {
  for (const auto& lhs_hp : lhs) {
    for (const auto& rhs_hp : rhs) {
      if (HasSameHostPort(lhs_hp, rhs_hp)) {
        return true;
      }
    }
  }
  return false;
}

bool HasSameHostPort(
    const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs,
    const CloudInfoPB& cloud_info) {
  return HasSameHostPort(DesiredHostPort(lhs, cloud_info), DesiredHostPort(rhs, cloud_info));
}

bool HasSameHostPort(const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs) {
  return HasSameHostPort(lhs.private_rpc_addresses(), rhs.private_rpc_addresses()) ||
         HasSameHostPort(lhs.broadcast_addresses(), rhs.broadcast_addresses());
}

bool HasSameHostPort(
    const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs,
    const std::vector<CloudInfoPB>& perspectives) {
  auto find_it = std::find_if(
      perspectives.begin(), perspectives.end(), [&lhs, &rhs](const CloudInfoPB& cloud_info) {
        return HasSameHostPort(lhs, rhs, cloud_info);
      });
  return find_it != perspectives.end();
}

// Returns a function to determine whether a registered ts matches the host port of the registering
// ts.
std::function<bool(const ServerRegistrationPB&)> GetHostPortCheckerFunction(
    const TSRegistrationPB& registration, const CloudInfoPB& local_cloud_info) {
  // This pivots on a runtime flag so we cannot return a static function.
  if (PREDICT_TRUE(GetAtomicFlag(&FLAGS_master_register_ts_check_desired_host_port))) {
    // When desired host-port check is enabled, we do the following checks:
    // 1. For master, the host-port for existing and registering tservers are different.
    // 2. The existing and registering tservers have distinct host-port from each others
    // perspective.
    return
        [&registration, &local_cloud_info](const ServerRegistrationPB& existing_ts_registration) {
          auto cloud_info_perspectives = {
              local_cloud_info,                       // master's perspective
              existing_ts_registration.cloud_info(),  // existing ts' perspective
              registration.common().cloud_info()};    // registering ts' perspective
          return HasSameHostPort(
              existing_ts_registration, registration.common(), cloud_info_perspectives);
        };
  } else {
    return [&registration](const ServerRegistrationPB& existing_ts_registration) {
      return HasSameHostPort(existing_ts_registration, registration.common());
    };
  }
}

}  // namespace

Result<TSDescriptorPtr> TSManager::LookupTS(const NodeInstancePB& instance) const {
  SharedLock<decltype(map_lock_)> l(map_lock_);

  const TSDescriptorPtr* found_ptr =
    FindOrNull(servers_by_id_, instance.permanent_uuid());
  if (!found_ptr) {
    return STATUS_FORMAT(
        NotFound, "Unknown tablet server ID not in map, instance data: $0",
        instance.ShortDebugString());
  }
  const TSDescriptorPtr& found = *found_ptr;
  auto desc_lock = found->LockForRead();
  if (desc_lock->pb.state() == SysTServerEntryPB::REPLACED) {
    return STATUS_FORMAT(
        NotFound,
        "Trying to lookup replaced tablet server, instance data: $0, entry: $1",
        instance.ShortDebugString(), found->ToString());
  }

  if (instance.instance_seqno() != desc_lock->pb.instance_seqno()) {
    return STATUS_FORMAT(
        NotFound, "mismatched instance sequence number $0, instance $1", found->latest_seqno(),
        instance.ShortDebugString());
  }

  return *found_ptr;
}

// todo(zdrudi): this function can create dangling references when we begin removing TSDescriptors
// from the registry.
bool TSManager::LookupTSByUUID(const std::string& uuid,
                               TSDescriptorPtr* ts_desc) {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  const TSDescriptorPtr* found_ptr = FindOrNull(servers_by_id_, uuid);
  if (!found_ptr || (*found_ptr)->IsRemoved()) {
    return false;
  }
  *ts_desc = *found_ptr;
  return true;
}

Result<std::pair<std::vector<TSDescriptorPtr>, std::vector<TSDescriptor::WriteLock>>>
TSManager::FindHostPortCollisions(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    const CloudInfoPB& local_cloud_info) const {
  auto hostport_checker = GetHostPortCheckerFunction(registration, local_cloud_info);
  std::vector<TSDescriptorPtr> descs;
  std::vector<TSDescriptor::WriteLock> locks;
  for (const auto& [_, ts_desc] : servers_by_id_) {
    if (ts_desc->permanent_uuid() == instance.permanent_uuid()) {
      continue;
    }
    // Acquire write locks because we may have to mutate later.
    auto l = ts_desc->LockForWrite();
    if (!hostport_checker(l->pb.registration())) {
      continue;
    }
    if (l->pb.instance_seqno() >= instance.instance_seqno()) {
      return STATUS_FORMAT(
          AlreadyPresent, "Cannot register TS $0 $1, host port collision with existing TS $2",
          instance.ShortDebugString(), registration.common().ShortDebugString(),
          l->pb.ShortDebugString());
    }
    l.mutable_data()->pb.set_state(SysTServerEntryPB::REPLACED);
    descs.push_back(ts_desc);
    locks.push_back(std::move(l));
  }
  return std::make_pair(std::move(descs), std::move(locks));
}

Result<TSManager::RegistrationMutationData> TSManager::ComputeRegistrationMutationData(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    CloudInfoPB&& local_cloud_info, rpc::ProxyCache* proxy_cache,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  TSManager::RegistrationMutationData reg_data;
  {
    SharedLock<decltype(map_lock_)> map_l(map_lock_);
    const std::string& uuid = instance.permanent_uuid();
    // Find any already registered tservers that have conflicting addresses with the registering
    // tserver.
    std::tie(reg_data.replaced_descs, reg_data.replaced_desc_locks) =
        VERIFY_RESULT(FindHostPortCollisions(instance, registration, local_cloud_info));
    auto it = servers_by_id_.find(uuid);
    if (it == servers_by_id_.end()) {
      // We have no entry for this uuid. Create a new TSDescriptor and make a note to add it
      // to the registry.
      std::tie(reg_data.desc, reg_data.registered_desc_lock) =
          VERIFY_RESULT(TSDescriptor::CreateNew(
              instance, registration, std::move(local_cloud_info), proxy_cache,
              registered_through_heartbeat));
      reg_data.insert_into_map = true;
    } else {
      // This tserver has registered before. We just need to update its registration metadata.
      reg_data.registered_desc_lock = VERIFY_RESULT(it->second->UpdateRegistration(
          instance, registration, std::move(local_cloud_info), proxy_cache));
      reg_data.desc = it->second;
    }
  }
  return reg_data;
}

Status TSManager::RegisterFromRaftConfig(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    CloudInfoPB&& local_cloud_info, rpc::ProxyCache* proxy_cache) {
  // todo(zdrudi): what happens if the registration through raft config is blocked because of a host
  // port collision? add a test.
  VERIFY_RESULT(
      RegisterInternal(instance, registration, {}, std::move(local_cloud_info), proxy_cache));
  return Status::OK();
}

Result<TSDescriptorPtr> TSManager::LookupAndUpdateTSFromHeartbeat(
    const TSHeartbeatRequestPB& heartbeat_request) const {
  auto desc = VERIFY_RESULT(LookupTS(heartbeat_request.common().ts_instance()));
  auto lock = desc->LockForWrite();
  RETURN_NOT_OK(desc->UpdateTSMetadataFromHeartbeat(heartbeat_request, &lock));
  // todo(zdrudi): write to sys catalog here. The write may be a no-op if the TServer was already
  // live.
  lock.Commit();
  return desc;
}

Result<TSDescriptorPtr> TSManager::RegisterFromHeartbeat(
    const TSHeartbeatRequestPB& heartbeat_request, CloudInfoPB&& local_cloud_info,
    rpc::ProxyCache* proxy_cache) {
  return RegisterInternal(
      heartbeat_request.common().ts_instance(), heartbeat_request.registration(),
      std::cref(heartbeat_request), std::move(local_cloud_info), proxy_cache);
}

Result<TSDescriptorPtr> TSManager::RegisterInternal(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    std::optional<std::reference_wrapper<const TSHeartbeatRequestPB>> request,
    CloudInfoPB&& local_cloud_info, rpc::ProxyCache* proxy_cache) {
  TSCountCallback callback;
  TSDescriptorPtr result;
  auto registered_through_heartbeat = RegisteredThroughHeartbeat(request.has_value());
  {
    MutexLock l(registration_lock_);
    auto reg_data = VERIFY_RESULT(ComputeRegistrationMutationData(
        instance, registration, std::move(local_cloud_info), proxy_cache,
        registered_through_heartbeat));
    if (request.has_value()) {
      RETURN_NOT_OK(reg_data.desc->UpdateTSMetadataFromHeartbeat(
          *request, &reg_data.registered_desc_lock));
    }
    std::tie(result, callback) =
        VERIFY_RESULT(DoRegistrationMutation(instance, registration, std::move(reg_data)));
  }
  if (callback) {
    callback();
  }
  return result;
}

Result<std::pair<TSDescriptorPtr, TSCountCallback>> TSManager::DoRegistrationMutation(
    const NodeInstancePB& new_ts_instance, const TSRegistrationPB& registration,
    TSManager::RegistrationMutationData&& registration_data) {
  TSCountCallback callback;
  // todo(zdrudi): write all descs in registration to sys catalog here.
  registration_data.registered_desc_lock.Commit();
  for (auto& l : registration_data.replaced_desc_locks) {
    LOG(WARNING) << "Removing entry: " << l->pb.ShortDebugString()
                 << " since we received registration for a tserver with a higher sequence number: "
                 << new_ts_instance.ShortDebugString();
    l.Commit();
  }
  if (registration_data.insert_into_map) {
    std::lock_guard map_l(map_lock_);
    InsertOrDie(&servers_by_id_, new_ts_instance.permanent_uuid(), registration_data.desc);
    LOG(INFO) << "Registered new tablet server { " << new_ts_instance.ShortDebugString()
              << " } with Master, full list: " << yb::ToString(servers_by_id_);
    if (ts_count_callback_) {
      auto new_count = NumDescriptorsUnlocked();
      if (new_count >= ts_count_callback_min_count_) {
        callback = std::move(ts_count_callback_);
        ts_count_callback_min_count_ = 0;
      }
    }
  } else {
    LOG(INFO) << "Re-registered known tablet server { " << new_ts_instance.ShortDebugString()
              << " }: " << registration.ShortDebugString();
  }

  return std::make_pair(std::move(registration_data.desc), std::move(callback));
}

void TSManager::GetDescriptors(std::function<bool(const TSDescriptorPtr&)> condition,
                               TSDescriptorVector* descs) const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  GetDescriptorsUnlocked(condition, descs);
}

void TSManager::GetDescriptorsUnlocked(
    std::function<bool(const TSDescriptorPtr&)> condition, TSDescriptorVector* descs) const {
  descs->clear();

  descs->reserve(servers_by_id_.size());
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescriptorPtr& ts = entry.second;
    if (condition(ts)) {
      VLOG(1) << " Adding " << yb::ToString(*ts);
      descs->push_back(ts);
    } else {
      VLOG(1) << " NOT Adding " << yb::ToString(*ts);
    }
  }
}

void TSManager::GetAllDescriptors(TSDescriptorVector* descs) const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  GetAllDescriptorsUnlocked(descs);
}

TSDescriptorVector TSManager::GetAllDescriptors() const {
  TSDescriptorVector descs;
  GetAllDescriptors(&descs);
  return descs;
}

void TSManager::GetAllDescriptorsUnlocked(TSDescriptorVector* descs) const {
  GetDescriptorsUnlocked([](const TSDescriptorPtr& ts) -> bool { return !ts->IsRemoved(); }, descs);
}

void TSManager::GetAllLiveDescriptors(TSDescriptorVector* descs,
                                      const std::optional<BlacklistSet>& blacklist) const {
  GetDescriptors([blacklist](const TSDescriptorPtr& ts) -> bool {
    return ts->IsLive() && !IsTsBlacklisted(ts, blacklist); }, descs);
}

void TSManager::GetAllReportedDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescriptorPtr& ts)
                   -> bool { return ts->IsLive() && ts->has_tablet_report(); }, descs);
}

bool TSManager::IsTsInCluster(const TSDescriptorPtr& ts, const std::string& cluster_uuid) {
  return ts->placement_uuid() == cluster_uuid;
}

bool TSManager::IsTsBlacklisted(const TSDescriptorPtr& ts,
                                const std::optional<BlacklistSet>& blacklist) {
  return blacklist.has_value() && ts->IsBlacklisted(*blacklist);
}

void TSManager::GetAllLiveDescriptorsInCluster(TSDescriptorVector* descs,
    const std::string& placement_uuid,
    const std::optional<BlacklistSet>& blacklist,
    bool primary_cluster) const {
  descs->clear();
  SharedLock<decltype(map_lock_)> l(map_lock_);

  descs->reserve(servers_by_id_.size());
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescriptorPtr& ts = entry.second;
    // ts_in_cluster true if there's a matching config and tserver placement uuid or
    // if we're getting primary nodes and the tserver placement uuid is empty.
    bool ts_in_cluster = (IsTsInCluster(ts, placement_uuid) ||
                         (primary_cluster && ts->placement_uuid().empty()));
    if (ts->IsLive() && !IsTsBlacklisted(ts, blacklist) && ts_in_cluster) {
      descs->push_back(ts);
    }
  }
}

size_t TSManager::NumDescriptorsUnlocked() const {
  return std::count_if(
      servers_by_id_.cbegin(), servers_by_id_.cend(),
      [](const auto& entry) -> bool { return !entry.second->IsRemoved(); });
}

// Register a callback to be called when the number of tablet servers reaches a certain number.
void TSManager::SetTSCountCallback(int min_count, TSCountCallback callback) {
  std::lock_guard l(registration_lock_);
  ts_count_callback_ = std::move(callback);
  ts_count_callback_min_count_ = min_count;
}

size_t TSManager::NumDescriptors() const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  return NumDescriptorsUnlocked();
}

size_t TSManager::NumLiveDescriptors() const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  return std::count_if(
      servers_by_id_.cbegin(), servers_by_id_.cend(),
      [](const auto& entry) -> bool { return entry.second->IsLive(); });
}

void TSManager::MarkUnresponsiveTServers() {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  auto current_time = MonoTime::Now();
  auto unresponsive_timeout_millis = GetAtomicFlag(&FLAGS_tserver_unresponsive_timeout_ms);
  std::vector<TSDescriptor::WriteLock> locks;
  for (const auto& [id, desc] : servers_by_id_) {
    auto last_heartbeat_time = desc->LastHeartbeatTime();
    if (last_heartbeat_time && current_time.GetDeltaSince(last_heartbeat_time).ToMilliseconds() <
                                   unresponsive_timeout_millis) {
      continue;
    }
    auto desc_lock = desc->LockForWrite();
    if (desc_lock->pb.state() == SysTServerEntryPB::MAYBE_LIVE) {
      desc_lock.mutable_data()->pb.set_state(SysTServerEntryPB::UNRESPONSIVE);
      locks.push_back(std::move(desc_lock));
    }
  }
  // todo(zdrudi): write to sys catalog here
  for (auto& lock : locks) {
    lock.Commit();
  }
}

} // namespace master
} // namespace yb
