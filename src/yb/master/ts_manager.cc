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
                           shared_ptr<TSDescriptor>* ts_desc) {
  boost::shared_lock<rw_spinlock> l(lock_);
  const shared_ptr<TSDescriptor>* found_ptr =
    FindOrNull(servers_by_id_, instance.permanent_uuid());
  if (!found_ptr) {
    return STATUS(NotFound, "unknown tablet server ID", instance.ShortDebugString());
  }
  const shared_ptr<TSDescriptor>& found = *found_ptr;

  if (instance.instance_seqno() != found->latest_seqno()) {
    return STATUS(NotFound, "mismatched instance sequence number", instance.ShortDebugString());
  }

  *ts_desc = found;
  return Status::OK();
}

bool TSManager::LookupTSByUUID(const string& uuid,
                               std::shared_ptr<TSDescriptor>* ts_desc) {
  boost::shared_lock<rw_spinlock> l(lock_);
  return FindCopy(servers_by_id_, uuid, ts_desc);
}

Status TSManager::RegisterTS(const NodeInstancePB& instance,
                             const TSRegistrationPB& registration,
                             std::shared_ptr<TSDescriptor>* desc) {
  std::lock_guard<rw_spinlock> l(lock_);
  const string& uuid = instance.permanent_uuid();

  if (!ContainsKey(servers_by_id_, uuid)) {
    gscoped_ptr<TSDescriptor> new_desc;
    RETURN_NOT_OK(TSDescriptor::RegisterNew(instance, registration, &new_desc));
    InsertOrDie(&servers_by_id_, uuid, shared_ptr<TSDescriptor>(new_desc.release()));
    LOG(INFO) << "Registered new tablet server { " << instance.ShortDebugString()
              << " } with Master, full list: " << yb::ToString(servers_by_id_);
  } else {
    const shared_ptr<TSDescriptor>& found = FindOrDie(servers_by_id_, uuid);
    RETURN_NOT_OK(found->Register(instance, registration));
    LOG(INFO) << "Re-registered known tablet server { " << instance.ShortDebugString()
              << " } with Master";
  }

  return Status::OK();
}

void TSManager::GetAllDescriptors(vector<shared_ptr<TSDescriptor> > *descs) const {
  descs->clear();
  boost::shared_lock<rw_spinlock> l(lock_);
  AppendValuesFromMap(servers_by_id_, descs);
}

bool TSManager::IsTSLive(const shared_ptr<TSDescriptor>& ts) const {
  return ts->TimeSinceHeartbeat().ToMilliseconds() <
         GetAtomicFlag(&FLAGS_tserver_unresponsive_timeout_ms);
}

void TSManager::GetAllLiveDescriptors(vector<shared_ptr<TSDescriptor> > *descs) const {
  descs->clear();

  boost::shared_lock<rw_spinlock> l(lock_);
  descs->reserve(servers_by_id_.size());
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const shared_ptr<TSDescriptor>& ts = entry.second;
    if (IsTSLive(ts)) {
      descs->push_back(ts);
    }
  }
}

const std::shared_ptr<TSDescriptor> TSManager::GetTSDescriptor(const HostPortPB& host_port) const {
  boost::shared_lock<rw_spinlock> l(lock_);
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const shared_ptr<TSDescriptor>& ts = entry.second;
    if (IsTSLive(ts) && ts->IsRunningOn(host_port)) {
      return ts;
    }
  }

  return nullptr;
}

int TSManager::GetCount() const {
  boost::shared_lock<rw_spinlock> l(lock_);
  return servers_by_id_.size();
}

} // namespace master
} // namespace yb

