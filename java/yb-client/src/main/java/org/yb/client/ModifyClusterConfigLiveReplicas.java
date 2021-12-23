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

public class ModifyClusterConfigLiveReplicas extends AbstractModifyMasterClusterConfig {

  // The PlacementInfoPB for live replicas. We use the full PB so the caller has full control over
  // the placement without creating a class for each field to be modified.
  private PlacementInfoPB liveReplicas;

  public ModifyClusterConfigLiveReplicas(YBClient client, PlacementInfoPB liveReplicas) {
    super(client);
    this.liveReplicas = liveReplicas;
  }

  @Override
  protected SysClusterConfigEntryPB modifyConfig(SysClusterConfigEntryPB config) {
    SysClusterConfigEntryPB.Builder configBuilder =
        SysClusterConfigEntryPB.newBuilder(config);

    // Modify the live replicas field in the replication info.
    configBuilder.getReplicationInfoBuilder().setLiveReplicas(liveReplicas);

    return configBuilder.build();
  }
}
