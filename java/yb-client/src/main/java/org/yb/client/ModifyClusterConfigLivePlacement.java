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

import org.yb.master.CatalogEntityInfo.PlacementInfoPB;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;

// This provides the wrapper to read-modify the live placement for the cluster.
public class ModifyClusterConfigLivePlacement extends AbstractModifyMasterClusterConfig {
  private PlacementInfoPB placementInfo;
  public ModifyClusterConfigLivePlacement(YBClient client, PlacementInfoPB placementInfo) {
    super(client);
    this.placementInfo = placementInfo;
  }

  @Override
  protected SysClusterConfigEntryPB modifyConfig(SysClusterConfigEntryPB config) {
    SysClusterConfigEntryPB.Builder configBuilder = SysClusterConfigEntryPB.newBuilder(config);

    // Modify the live placement for this cluster.
    configBuilder.getReplicationInfoBuilder()
                 .setLiveReplicas(placementInfo)
                 .build();

    return configBuilder.build();
  }
}
