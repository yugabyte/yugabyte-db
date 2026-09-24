// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <string>

#include "yb/cdc/xcluster_types.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_entity_base.h"
#include "yb/master/sys_catalog.h"

namespace yb::master {

struct PersistentXClusterConfigInfo : public Persistent<SysXClusterConfigEntryPB> {};

class XClusterConfigInfo : public SingletonMetadataCowWrapper<PersistentXClusterConfigInfo> {};

DECLARE_SINGLETON_LOADER_CLASS(XClusterConfig, std::string, SysXClusterConfigEntryPB);

struct PersistentXClusterSafeTimeInfo : public Persistent<XClusterSafeTimePB> {};

class XClusterSafeTimeInfo : public SingletonMetadataCowWrapper<PersistentXClusterSafeTimeInfo> {
 public:
  void Load(const XClusterSafeTimePB& metadata) override;
};

DECLARE_SINGLETON_LOADER_CLASS(XClusterSafeTime, std::string, XClusterSafeTimePB);

struct PersistentXClusterOutboundReplicationGroupInfo
    : public Persistent<SysXClusterOutboundReplicationGroupEntryPB> {};

class XClusterOutboundReplicationGroupInfo
    : public MetadataCowWrapper<PersistentXClusterOutboundReplicationGroupInfo> {
 public:
  explicit XClusterOutboundReplicationGroupInfo(
      const xcluster::ReplicationGroupId& replication_group_id)
      : replication_group_id_(replication_group_id) {}

  const std::string& id() const override { return replication_group_id_.ToString(); }
  const xcluster::ReplicationGroupId& ReplicationGroupId() const { return replication_group_id_; }

 private:
  const xcluster::ReplicationGroupId replication_group_id_;
};

DECLARE_MULTI_INSTANCE_LOADER_CLASS(
    XClusterOutboundReplicationGroup, std::string, SysXClusterOutboundReplicationGroupEntryPB);

}  // namespace yb::master
