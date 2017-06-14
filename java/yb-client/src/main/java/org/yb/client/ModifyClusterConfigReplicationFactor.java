// Copyright (c) YugaByte, Inc.

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
