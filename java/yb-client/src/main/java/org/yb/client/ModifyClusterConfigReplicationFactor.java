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

import java.util.Comparator;
import java.util.List;
import java.util.TreeSet;

import org.yb.annotations.InterfaceAudience;
import org.yb.Common.HostPortPB;
import org.yb.master.Master;

// This provides the wrapper to read-modify the replication factor.
public class ModifyClusterConfigReplicationFactor extends AbstractModifyMasterClusterConfig {
  private int replicationFactor;
  public ModifyClusterConfigReplicationFactor(YBClient client, int replFactor) {
    super(client);
    this.replicationFactor = replFactor;
  }

  @Override
  protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config) {
    Master.SysClusterConfigEntryPB.Builder configBuilder =
        Master.SysClusterConfigEntryPB.newBuilder(config);

    // Modify the num_replicas which is the cluster's RF.
    configBuilder.getReplicationInfoBuilder()
                 .getLiveReplicasBuilder()
                 .setNumReplicas(this.replicationFactor)
                 .build();

    return configBuilder.build();
  }
}
