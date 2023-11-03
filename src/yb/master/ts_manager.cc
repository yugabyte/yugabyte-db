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

namespace yb {
namespace master {

TSManager::TSManager() {
}

TSManager::~TSManager() {
}

Status TSManager::LookupTS(const NodeInstancePB& instance,
                           TSDescriptorPtr* ts_desc) {
  SharedLock<decltype(lock_)> l(lock_);

  const TSDescriptorPtr* found_ptr =
    FindOrNull(servers_by_id_, instance.permanent_uuid());
  if (!found_ptr || (*found_ptr)->IsRemoved()) {
    return STATUS_FORMAT(
        NotFound,
        "unknown tablet server ID, server is in map: $0, server is removed: $1, instance data: $2",
        found_ptr != nullptr, found_ptr ? (*found_ptr)->IsRemoved() : false,
        instance.ShortDebugString());
  }
  const TSDescriptorPtr& found = *found_ptr;

  if (instance.instance_seqno() != found->latest_seqno()) {
    return STATUS_FORMAT(
        NotFound, "mismatched instance sequence number $0, instance $1", found->latest_seqno(),
        instance.ShortDebugString());
  }

  *ts_desc = found;
  return Status::OK();
}

bool TSManager::LookupTSByUUID(const string& uuid,
                               TSDescriptorPtr* ts_desc) {
  SharedLock<decltype(lock_)> l(lock_);
  const TSDescriptorPtr* found_ptr = FindOrNull(servers_by_id_, uuid);
  if (!found_ptr || (*found_ptr)->IsRemoved()) {
    return false;
  }
  *ts_desc = *found_ptr;
  return true;
}

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

std::vector<std::pair<TSDescriptor*, std::shared_ptr<TSInformationPB>>>
TSManager::FindHostPortMatches(
    const NodeInstancePB& instance,
    const TSRegistrationPB& registration,
    const CloudInfoPB& local_cloud_info) const {
  // Function to determine whether a registered ts matches the host port of the registering ts.
  std::function<bool(const ServerRegistrationPB&)> hostport_checker;
  if (PREDICT_TRUE(GetAtomicFlag(&FLAGS_master_register_ts_check_desired_host_port))) {
    // When desired host-port check is enabled, we do the following checks:
    // 1. For master, the host-port for existing and registering tservers are different.
    // 2. The existing and registering tservers have distinct host-port from each others
    // perspective.
    hostport_checker = [&registration,
                        &local_cloud_info](const ServerRegistrationPB& existing_ts_registration) {
      auto cloud_info_perspectives = {
          local_cloud_info,                       // master's perspective
          existing_ts_registration.cloud_info(),  // existing ts' perspective
          registration.common().cloud_info()};    // registering ts' perspective
      return HasSameHostPort(
          existing_ts_registration, registration.common(), cloud_info_perspectives);
    };
  } else {
    hostport_checker = [&registration](const ServerRegistrationPB& existing_ts_registration) {
      return HasSameHostPort(existing_ts_registration, registration.common());
    };
  }
  std::vector<std::pair<TSDescriptor*, std::shared_ptr<TSInformationPB>>> matches;
  for (const auto& map_entry : servers_by_id_) {
    const auto existing_ts_info = map_entry.second->GetTSInformationPB();
    const auto& existing_ts_registration_common = existing_ts_info->registration().common();
    if (existing_ts_info->tserver_instance().permanent_uuid() == instance.permanent_uuid()) {
      continue;
    }
    if (hostport_checker(existing_ts_registration_common)) {
      matches.push_back(std::pair(map_entry.second.get(), std::move(existing_ts_info)));
    }
  }
  return matches;
}

Status TSManager::RegisterTS(
    const NodeInstancePB& instance,
    const TSRegistrationPB& registration,
    CloudInfoPB local_cloud_info,
    rpc::ProxyCache* proxy_cache,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  TSCountCallback callback_to_call;
  {
    std::lock_guard l(lock_);
    const string& uuid = instance.permanent_uuid();
    auto duplicate_hostport_ts_descriptors =
        FindHostPortMatches(instance, registration, local_cloud_info);
    for (const auto& [ts, ts_info] : duplicate_hostport_ts_descriptors) {
      if (ts_info->tserver_instance().instance_seqno() >= instance.instance_seqno()) {
        // Skip adding the node since we already have a node with the same rpc address and
        // a higher sequence number.
        LOG(WARNING) << "Skipping registration for TS " << instance.ShortDebugString()
                     << " since an entry with same host/port but a higher sequence number exists "
                     << ts_info->ShortDebugString();
        return Status::OK();
      } else {
        LOG(WARNING)
            << "Removing entry: " << ts_info->ShortDebugString()
            << " since we received registration for a tserver with a higher sequence number: "
            << instance.ShortDebugString();
        // Mark the old node to be removed, since we have a newer sequence number.
        ts->SetRemoved();
      }
    }
    auto it = servers_by_id_.find(uuid);
    if (it == servers_by_id_.end()) {
      auto new_desc = VERIFY_RESULT(TSDescriptor::RegisterNew(
          instance, registration, std::move(local_cloud_info), proxy_cache,
          registered_through_heartbeat));
      InsertOrDie(&servers_by_id_, uuid, std::move(new_desc));
      LOG(INFO) << "Registered new tablet server { " << instance.ShortDebugString()
                << " } with Master, full list: " << yb::ToString(servers_by_id_);
    } else {
      RETURN_NOT_OK(
          it->second->Register(instance, registration, std::move(local_cloud_info), proxy_cache));
      it->second->SetRemoved(false);
      LOG(INFO) << "Re-registered known tablet server { " << instance.ShortDebugString()
                << " }: " << registration.ShortDebugString();
    }
    if (!ts_count_callback_.empty()) {
      auto new_count = GetCountUnlocked();
      if (new_count >= ts_count_callback_min_count_) {
        callback_to_call = std::move(ts_count_callback_);
        ts_count_callback_min_count_ = 0;
      }
    }
  }
  if (!callback_to_call.empty()) {
    callback_to_call();
  }
  return Status::OK();
}

void TSManager::GetDescriptors(std::function<bool(const TSDescriptorPtr&)> condition,
                               TSDescriptorVector* descs) const {
  SharedLock<decltype(lock_)> l(lock_);
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
  SharedLock<decltype(lock_)> l(lock_);
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
                                      const boost::optional<BlacklistSet>& blacklist) const {
  GetDescriptors([blacklist](const TSDescriptorPtr& ts) -> bool {
    return ts->IsLive() && !IsTsBlacklisted(ts, blacklist); }, descs);
}

void TSManager::GetAllReportedDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescriptorPtr& ts)
                   -> bool { return ts->IsLive() && ts->has_tablet_report(); }, descs);
}

bool TSManager::IsTsInCluster(const TSDescriptorPtr& ts, string cluster_uuid) {
  return ts->placement_uuid() == cluster_uuid;
}

bool TSManager::IsTsBlacklisted(const TSDescriptorPtr& ts,
                                const boost::optional<BlacklistSet>& blacklist) {
  if (!blacklist.is_initialized()) {
    return false;
  }
  return ts->IsBlacklisted(*blacklist);
}

void TSManager::GetAllLiveDescriptorsInCluster(TSDescriptorVector* descs,
    string placement_uuid,
    const boost::optional<BlacklistSet>& blacklist,
    bool primary_cluster) const {
  descs->clear();
  SharedLock<decltype(lock_)> l(lock_);

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

const TSDescriptorPtr TSManager::GetTSDescriptor(const HostPortPB& host_port) const {
  SharedLock<decltype(lock_)> l(lock_);

  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescriptorPtr& ts = entry.second;
    if (ts->IsLive() && ts->IsRunningOn(host_port)) {
      return ts;
    }
  }

  return nullptr;
}

size_t TSManager::GetCountUnlocked() const {
  TSDescriptorVector descs;
  GetAllDescriptorsUnlocked(&descs);
  return descs.size();
}

// Register a callback to be called when the number of tablet servers reaches a certain number.
void TSManager::SetTSCountCallback(int min_count, TSCountCallback callback) {
  std::lock_guard l(lock_);
  ts_count_callback_ = std::move(callback);
  ts_count_callback_min_count_ = min_count;
}

} // namespace master
} // namespace yb
