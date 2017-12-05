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

Status TSManager::RegisterTS(const NodeInstancePB& instance,
                             const TSRegistrationPB& registration,
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

      for (const auto& existing_rpc_address : current_registration_pb.common().rpc_addresses()) {
        for (const auto& new_rpc_address : registration.common().rpc_addresses()) {
          if (new_rpc_address.host() == existing_rpc_address.host() &&
              new_rpc_address.port() == existing_rpc_address.port()) {
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
      }
    }

    gscoped_ptr<TSDescriptor> new_desc;
    RETURN_NOT_OK(TSDescriptor::RegisterNew(instance, registration, &new_desc));
    InsertOrDie(&servers_by_id_, uuid, TSDescSharedPtr(new_desc.release()));
    LOG(INFO) << "Registered new tablet server { " << instance.ShortDebugString()
              << " } with Master, full list: " << yb::ToString(servers_by_id_);
  } else {
    const TSDescSharedPtr& found = FindOrDie(servers_by_id_, uuid);
    RETURN_NOT_OK(found->Register(instance, registration));
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

void TSManager::GetAllLiveDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescSharedPtr& ts) -> bool { return IsTSLive(ts); }, descs);
}

void TSManager::GetAllReportedDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescSharedPtr& ts)
                   -> bool { return IsTSLive(ts) && ts->has_tablet_report(); }, descs);
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

