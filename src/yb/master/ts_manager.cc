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

#include <boost/thread/shared_mutex.hpp>
#include "yb/gutil/map-util.h"
#include "yb/master/master.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/util/flag_tags.h"
#include "yb/common/wire_protocol.h"

DEFINE_int32(tserver_unresponsive_timeout_ms, 60 * 1000,
             "The period of time that a Master can go without receiving a heartbeat from a "
             "tablet server before considering it unresponsive. Unresponsive servers are not "
             "selected when assigning replicas during table creation or re-replication.");
TAG_FLAG(tserver_unresponsive_timeout_ms, advanced);

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
                           TSDescSharedPtr* ts_desc) {
  boost::shared_lock<rw_spinlock> l(lock_);
  const TSDescSharedPtr* found_ptr =
    FindOrNull(servers_by_id_, instance.permanent_uuid());
  if (!found_ptr || (*found_ptr)->IsRemoved()) {
    return STATUS(NotFound, "unknown tablet server ID", instance.ShortDebugString());
  }
  const TSDescSharedPtr& found = *found_ptr;

  if (instance.instance_seqno() != found->latest_seqno()) {
    return STATUS(NotFound, "mismatched instance sequence number", instance.ShortDebugString());
  }

  *ts_desc = found;
  return Status::OK();
}

bool TSManager::LookupTSByUUID(const string& uuid,
                               TSDescSharedPtr* ts_desc) {
  boost::shared_lock<rw_spinlock> l(lock_);
  const TSDescSharedPtr* found_ptr = FindOrNull(servers_by_id_, uuid);
  if (!found_ptr || (*found_ptr)->IsRemoved()) {
    return false;
  }
  *ts_desc = *found_ptr;
  return true;
}

bool HasSameHostPort(const google::protobuf::RepeatedPtrField<HostPortPB>& old_addresses,
                     const google::protobuf::RepeatedPtrField<HostPortPB>& new_addresses) {
  for (const auto& old_address : old_addresses) {
    for (const auto& new_address : new_addresses) {
      if (old_address.host() == new_address.host() && old_address.port() == new_address.port())
        return true;
    }
  }

  return false;
}

Status TSManager::RegisterTS(const NodeInstancePB& instance,
                             const TSRegistrationPB& registration,
                             CloudInfoPB local_cloud_info,
                             rpc::ProxyCache* proxy_cache,
                             TSDescSharedPtr* desc) {
  std::lock_guard<rw_spinlock> l(lock_);
  const string& uuid = instance.permanent_uuid();

  if (!ContainsKey(servers_by_id_, uuid)) {
    // Check if a server with the same host and port already exists.
    for (const auto& map_entry : servers_by_id_) {
      NodeInstancePB current_instance_pb;
      TSRegistrationPB current_registration_pb;
      map_entry.second->GetNodeInstancePB(&current_instance_pb);
      map_entry.second->GetRegistration(&current_registration_pb);

      if (HasSameHostPort(current_registration_pb.common().private_rpc_addresses(),
                          registration.common().private_rpc_addresses()) ||
          HasSameHostPort(current_registration_pb.common().broadcast_addresses(),
                          registration.common().broadcast_addresses())) {
        if (current_instance_pb.instance_seqno() >= instance.instance_seqno()) {
          // Skip adding the node since we already have a node with the same rpc address and
          // a higher sequence number.
          LOG(WARNING) << "Skipping registration for TS " << instance.ShortDebugString()
              << " since an entry with same host/port but a higher sequence number exists "
              << current_instance_pb.ShortDebugString();
          return Status::OK();
        } else {
          LOG(WARNING) << "Removing entry: " << current_instance_pb.ShortDebugString()
              << " since we received registration for a tserver with a higher sequence number: "
              << instance.ShortDebugString();
          // Mark the old node to be removed, since we have a newer sequence number.
          map_entry.second->SetRemoved();
        }
      }
    }

    auto new_desc = VERIFY_RESULT(TSDescriptor::RegisterNew(
        instance, registration, std::move(local_cloud_info), proxy_cache));
    InsertOrDie(&servers_by_id_, uuid, TSDescSharedPtr(new_desc.release()));
    LOG(INFO) << "Registered new tablet server { " << instance.ShortDebugString()
              << " } with Master, full list: " << yb::ToString(servers_by_id_);
  } else {
    const TSDescSharedPtr& found = FindOrDie(servers_by_id_, uuid);
    RETURN_NOT_OK(found->Register(
        instance, registration, std::move(local_cloud_info), proxy_cache));
    LOG(INFO) << "Re-registered known tablet server { " << instance.ShortDebugString()
              << " } with Master";
  }

  return Status::OK();
}

void TSManager::GetDescriptors(std::function<bool(const TSDescSharedPtr&)> condition,
                               TSDescriptorVector* descs) const {
  descs->clear();
  boost::shared_lock<rw_spinlock> l(lock_);
  descs->reserve(servers_by_id_.size());
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescSharedPtr& ts = entry.second;
    if (condition(ts)) {
      descs->push_back(ts);
    }
  }
}

void TSManager::GetAllDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescSharedPtr& ts) -> bool { return !ts->IsRemoved(); }, descs);
}

bool TSManager::IsTSLive(const TSDescSharedPtr& ts) {
  return ts->TimeSinceHeartbeat().ToMilliseconds() <
         GetAtomicFlag(&FLAGS_tserver_unresponsive_timeout_ms) && !ts->IsRemoved();
}

void TSManager::GetAllLiveDescriptors(TSDescriptorVector* descs,
    const BlacklistSet blacklist) const {
  GetDescriptors([blacklist](const TSDescSharedPtr& ts) -> bool {
    return IsTSLive(ts) && !IsTsBlacklisted(ts, blacklist); }, descs);
}

void TSManager::GetAllReportedDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescSharedPtr& ts)
                   -> bool { return IsTSLive(ts) && ts->has_tablet_report(); }, descs);
}

bool TSManager::IsTsInCluster(const TSDescSharedPtr& ts, string cluster_uuid) {
    return cluster_uuid.empty() || ts->placement_uuid() == cluster_uuid;
}

bool TSManager::IsTsBlacklisted(const TSDescSharedPtr& ts,
    const BlacklistSet blacklist) {
  if (blacklist.empty()) {
    return false;
  }
  for (const auto tserver : blacklist) {
    HostPortPB hp;
    HostPortToPB(tserver, &hp);
    if (ts->IsRunningOn(hp)) {
      return true;
    }
  }
  return false;
}

void TSManager::GetAllLiveDescriptorsInCluster(TSDescriptorVector* descs,
    string placement_uuid,
    const BlacklistSet blacklist) const {
  descs->clear();
  boost::shared_lock<rw_spinlock> l(lock_);
  descs->reserve(servers_by_id_.size());
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescSharedPtr& ts = entry.second;
    if (IsTSLive(ts) && IsTsInCluster(ts, placement_uuid) && !IsTsBlacklisted(ts, blacklist)) {
      descs->push_back(ts);
    }
  }
}

const TSDescSharedPtr TSManager::GetTSDescriptor(const HostPortPB& host_port) const {
  boost::shared_lock<rw_spinlock> l(lock_);
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescSharedPtr& ts = entry.second;
    if (IsTSLive(ts) && ts->IsRunningOn(host_port)) {
      return ts;
    }
  }

  return nullptr;
}

int TSManager::GetCount() const {
  boost::shared_lock<rw_spinlock> l(lock_);
  size_t count = 0;
  for (const auto& map_entry : servers_by_id_) {
    if (!map_entry.second->IsRemoved()) {
      count++;
    }
  }
  return count;
}

} // namespace master
} // namespace yb

