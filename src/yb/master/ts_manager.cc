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

#include "yb/gutil/map-util.h"

#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/ts_descriptor.h"

using std::shared_ptr;
using std::string;
using std::vector;

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

bool HasSameHostPort(const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs) {
  return HasSameHostPort(lhs.private_rpc_addresses(), rhs.private_rpc_addresses()) ||
         HasSameHostPort(lhs.broadcast_addresses(), rhs.broadcast_addresses());
}

Status TSManager::RegisterTS(
    const NodeInstancePB& instance,
    const TSRegistrationPB& registration,
    CloudInfoPB local_cloud_info,
    rpc::ProxyCache* proxy_cache,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  TSCountCallback callback_to_call;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    const string& uuid = instance.permanent_uuid();
    // Check if a server with the same host and port already exists.
    for (const auto& map_entry : servers_by_id_) {
      const auto ts_info = map_entry.second->GetTSInformationPB();
      if (ts_info->tserver_instance().permanent_uuid() == instance.permanent_uuid()) {
        continue;
      }
      if (HasSameHostPort(ts_info->registration().common(), registration.common())) {
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
          map_entry.second->SetRemoved();
        }
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
  std::lock_guard<rw_spinlock> l(lock_);
  ts_count_callback_ = std::move(callback);
  ts_count_callback_min_count_ = min_count;
}

} // namespace master
} // namespace yb
