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

#include "yb/cdc/xcluster_types.h"
#include "yb/cdc/xrepl_types.h"
#include "yb/common/entity_ids_types.h"

namespace yb {

namespace master {

struct InboundXClusterReplicationGroupTableStatus {
  TableId source_table_id;
  xrepl::StreamId stream_id = xrepl::StreamId::Nil();
  TableId target_table_id;
  uint32 source_tablet_count = 0;
  uint32 target_tablet_count = 0;
  bool local_tserver_optimized = false;
  uint32 source_schema_version = 0;
  uint32 target_schema_version = 0;
  std::string status;
};

struct XClusterInboundReplicationGroupStatus {
  xcluster::ReplicationGroupId replication_group_id;
  std::string state;
  bool transactional = false;
  std::string master_addrs;
  bool disable_stream = false;
  uint32 compatible_auto_flag_config_version = 0;
  uint32 validated_remote_auto_flags_config_version = 0;
  uint32 validated_local_auto_flags_config_version = 0;
  std::string db_scoped_info;
  std::vector<InboundXClusterReplicationGroupTableStatus> table_statuses;
};

class XClusterOutboundTableStreamStatus {
 public:
  TableId table_id;
  xrepl::StreamId stream_id = xrepl::StreamId::Nil();
  std::string state;
};

class XClusterOutboundReplicationGroupTableStatus : public XClusterOutboundTableStreamStatus {
 public:
  void operator=(const XClusterOutboundTableStreamStatus& other) {
    *static_cast<XClusterOutboundTableStreamStatus*>(this) = other;
  }
  bool is_checkpointing = false;
  bool is_part_of_initial_bootstrap = false;
};

struct XClusterOutboundReplicationGroupNamespaceStatus {
  NamespaceId namespace_id;
  NamespaceName namespace_name;
  std::string state;
  bool initial_bootstrap_required = false;
  std::string status;
  std::vector<XClusterOutboundReplicationGroupTableStatus> table_statuses;
};

struct XClusterOutboundReplicationGroupStatus {
  xcluster::ReplicationGroupId replication_group_id;
  std::string state;
  std::string target_universe_info;
  std::vector<XClusterOutboundReplicationGroupNamespaceStatus> namespace_statuses;
};

struct XClusterStatus {
  std::string role;
  bool transactional = false;

  std::vector<XClusterOutboundReplicationGroupStatus> outbound_replication_group_statuses;
  std::vector<XClusterOutboundTableStreamStatus> outbound_table_stream_statuses;
  std::vector<XClusterInboundReplicationGroupStatus> inbound_replication_group_statuses;

  bool IsEnabled() const {
    return !role.empty() || !outbound_replication_group_statuses.empty() ||
           !outbound_table_stream_statuses.empty() || !inbound_replication_group_statuses.empty();
  }
};

}  // namespace master
}  // namespace yb
