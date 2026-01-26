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
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"

namespace yb {

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace master {

class UniverseReplicationInfo;

// Helper class to handle AlterUniverseReplication RPC.
// This object will only live as long as the operation is in progress.
class AlterUniverseReplicationHelper {
 public:
  ~AlterUniverseReplicationHelper();

  static Status Alter(
      Master& master, CatalogManager& catalog_manager, const AlterUniverseReplicationRequestPB* req,
      AlterUniverseReplicationResponsePB* resp, const LeaderEpoch& epoch);

  struct AddTablesToReplicationData {
    xcluster::ReplicationGroupId replication_group_id;
    master::NamespaceIdentifierPB source_namespace_to_add;
    std::vector<TableId> source_table_ids_to_add;
    std::vector<xrepl::StreamId> source_bootstrap_ids_to_add;
    std::optional<TableId> target_table_id = std::nullopt;

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(
          replication_group_id, source_namespace_to_add, source_table_ids_to_add,
          source_bootstrap_ids_to_add, target_table_id);
    }

    bool HasSourceNamespaceToAdd() const { return !source_namespace_to_add.id().empty(); }
  };

  static Status AddTablesToReplication(
      Master& master, CatalogManager& catalog_manager, const AddTablesToReplicationData& data,
      const LeaderEpoch& epoch);

 private:
  AlterUniverseReplicationHelper(
      Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch);

  Status AlterUniverseReplication(
      const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp);

  Status UpdateProducerAddress(
      scoped_refptr<UniverseReplicationInfo> universe,
      const AlterUniverseReplicationRequestPB* req);

  Status AddTablesToReplication(
      scoped_refptr<UniverseReplicationInfo> universe,
      const AddTablesToReplicationData& add_table_data);

  Status RunSetupUniverseReplication(const XClusterSetupUniverseReplicationData& setup_data);

  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  XClusterManager& xcluster_manager_;
  const LeaderEpoch epoch_;

  DISALLOW_COPY_AND_ASSIGN(AlterUniverseReplicationHelper);
};

}  // namespace master

}  // namespace yb
