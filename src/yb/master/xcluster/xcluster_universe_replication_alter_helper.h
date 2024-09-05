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

 private:
  AlterUniverseReplicationHelper(
      Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch);

  Status AlterUniverseReplication(
      const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp);

  Status UpdateProducerAddress(
      scoped_refptr<UniverseReplicationInfo> universe,
      const AlterUniverseReplicationRequestPB* req);

  Status AddTablesToReplication(
      scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req,
      AlterUniverseReplicationResponsePB* resp);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  XClusterManager& xcluster_manager_;
  const LeaderEpoch epoch_;

  DISALLOW_COPY_AND_ASSIGN(AlterUniverseReplicationHelper);
};

}  // namespace master

}  // namespace yb
