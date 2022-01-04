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

import java.util.List;

import org.yb.CommonNet.CloudInfoPB;
import org.yb.master.CatalogEntityInfo;

/**
 * Class for changing the affinitized leader information for the master's cluster config.
 * Takes in the list of affinitized leaders to pass into the config.
 */
public class ModifyClusterConfigAffinitizedLeaders extends AbstractModifyMasterClusterConfig {
  // List of all azs where leaders should reside.
  private List<CloudInfoPB> affinitizedLeadersAZs;

  public ModifyClusterConfigAffinitizedLeaders(YBClient client,
                                               List<CloudInfoPB> affinitizedLeadersAZs) {
    super(client);
    this.affinitizedLeadersAZs = affinitizedLeadersAZs;
  }

  @Override
  protected CatalogEntityInfo.SysClusterConfigEntryPB modifyConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB config) {
    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder(config);

    // Modify the affinitized leaders information in the cluster config.
    configBuilder.getReplicationInfoBuilder().clearAffinitizedLeaders()
                 .addAllAffinitizedLeaders(this.affinitizedLeadersAZs)
                 .build();

    return configBuilder.build();
  }
}
