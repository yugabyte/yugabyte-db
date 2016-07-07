// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import java.util.Collection;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

import models.commissioner.InstanceInfo;
import org.yb.WireProtocol;
import org.yb.annotations.InterfaceAudience;
import org.yb.client.AbstractModifyMasterClusterConfig;
import org.yb.client.YBClient;
import org.yb.master.Master;

@InterfaceAudience.Public
public class CreateUniverseModifyConfig extends AbstractModifyMasterClusterConfig {
  UUID instanceUUID;

  public CreateUniverseModifyConfig(YBClient client, UUID instanceUUID) {
    super(client);
    this.instanceUUID = instanceUUID;
  }

  @Override
  protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config) {
    // For the create universe case, generate the current config.
    Master.SysClusterConfigEntryPB.Builder configBuilder =
        Master.SysClusterConfigEntryPB.newBuilder();
    Collection<InstanceInfo.NodeDetails> nodes = InstanceInfo.getNodeDetails(instanceUUID);
    Master.PlacementInfoPB.Builder placeInfo = configBuilder.getPlacementInfoBuilder();

    int numMasters = InstanceInfo.getMasters(instanceUUID).size();
    if (numMasters > nodes.size()) {
      throw new IllegalStateException("Expected to have only " + nodes.size() + " masters, but found "
                                      + numMasters);
    }

    Set<String> placementIds = new HashSet<String>();
    for (InstanceInfo.NodeDetails node : nodes) {
      // Add one entry per cloud/region/zone combination. For now, assume the concatenation of
      // the names is unique with no subname overlapping.
      String placementId = node.cloud + node.region + node.az;
      if (!placementIds.contains(placementId)) {
        Master.PlacementBlockPB.Builder pbb = Master.PlacementBlockPB.newBuilder();
        WireProtocol.CloudInfoPB.Builder ccb = WireProtocol.CloudInfoPB.newBuilder();
        ccb.setPlacementCloud(node.cloud)
           .setPlacementRegion(node.region)
           .setPlacementZone(node.az);
        pbb.setCloudInfo(ccb);
        placementIds.add(placementId);
        pbb.setMinNumReplicas(1);  // TODO: Get actual minimum counts.
        placeInfo.addPlacementBlocks(pbb);
      }
    }

    placeInfo.setNumReplicas(numMasters);
    placeInfo.build();
    return configBuilder.build();
  }
}
