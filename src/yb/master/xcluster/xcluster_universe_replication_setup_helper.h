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
};

Result<std::shared_ptr<XClusterInboundReplicationGroupSetupTaskIf>>
CreateSetupUniverseReplicationTask(
    Master& master, CatalogManager& catalog_manager, const SetupUniverseReplicationRequestPB* req,
    const LeaderEpoch& epoch);

Status ValidateMasterAddressesBelongToDifferentCluster(
    Master& master, const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses);

}  // namespace master

}  // namespace yb
