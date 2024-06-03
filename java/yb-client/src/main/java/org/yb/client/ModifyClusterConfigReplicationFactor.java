// Copyright (c) YugaByte, Inc.
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

package org.yb.client;

import org.yb.master.CatalogEntityInfo;

// This provides the wrapper to read-modify the replication factor.
public class ModifyClusterConfigReplicationFactor extends AbstractModifyMasterClusterConfig {
  private int replicationFactor;
  public ModifyClusterConfigReplicationFactor(YBClient client, int replFactor) {
    super(client);
    this.replicationFactor = replFactor;
  }

  @Override
  protected CatalogEntityInfo.SysClusterConfigEntryPB modifyConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB config) {
    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder(config);

    // Modify the num_replicas which is the cluster's RF.
    configBuilder.getReplicationInfoBuilder()
                 .getLiveReplicasBuilder()
                 .setNumReplicas(this.replicationFactor)
                 .build();

    return configBuilder.build();
  }
}
