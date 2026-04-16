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

#include <google/protobuf/repeated_field.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/status_fwd.h"

namespace yb {

namespace master {

class SetupUniverseReplicationRequestPB;
class SetupUniverseReplicationResponsePB;

struct XClusterSetupUniverseReplicationData {
  xcluster::ReplicationGroupId replication_group_id;
  google::protobuf::RepeatedPtrField<HostPortPB> source_masters;
  std::vector<TableId> source_table_ids;
  std::vector<TableId> target_table_ids;
  std::vector<xrepl::StreamId> stream_ids;
  bool transactional;
  std::vector<NamespaceId> source_namespace_ids;
  std::vector<NamespaceId> target_namespace_ids;
  bool automatic_ddl_mode;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        replication_group_id, source_masters, source_table_ids, target_table_ids, stream_ids,
        transactional, source_namespace_ids, target_namespace_ids, automatic_ddl_mode);
  }

  bool StreamIdsProvided() const { return !stream_ids.empty(); }
  bool TargetTableIdsProvided() const { return !target_table_ids.empty(); }
};

// Helper class to setup an inbound xCluster replication group.
// StartSetup should be called to start the task.
// On a successful setup, a UniverseReplication group will be created.
class XClusterInboundReplicationGroupSetupTaskIf {
 public:
  virtual ~XClusterInboundReplicationGroupSetupTaskIf() = default;

  virtual xcluster::ReplicationGroupId Id() const = 0;

  virtual void StartSetup() = 0;

  virtual IsOperationDoneResult DoneResult() const = 0;

  // Returns true if the setup was cancelled or it already failed, and false if the setup
  // already completed successfully.
  virtual bool TryCancel() = 0;

  virtual bool IsAlterReplication() const = 0;
};

Result<std::shared_ptr<XClusterInboundReplicationGroupSetupTaskIf>>
CreateSetupUniverseReplicationTask(
    Master& master, CatalogManager& catalog_manager, XClusterSetupUniverseReplicationData&& data,
    const LeaderEpoch& epoch);

Status ValidateMasterAddressesBelongToDifferentCluster(
    Master& master, const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses);

}  // namespace master

}  // namespace yb
