// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.forms.AbstractTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.WireProtocol;
import org.yb.client.AbstractModifyMasterClusterConfig;
import org.yb.client.ProtobufHelper;
import org.yb.client.YBClient;
import org.yb.master.Master;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;

import play.api.Play;

public class UpdatePlacementInfo extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpdatePlacementInfo.class);

  // The YB client.
  public YBClientService ybService = null;

  // Parameters for placement info update task.
  public static class Params extends AbstractTaskParams {
    // The cloud provider to get node details.
    public CloudType cloud;

    // The universe against which this node's details should be saved.
    public UUID universeUUID;

    // Number of replicas in the placement info.
    public int numReplicas;

    // If present, then we intend to decommission these nodes.
    public Set<String> blacklistNodes = null;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    String hostPorts = Universe.get(taskParams().universeUUID).getMasterAddresses();
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);

      ModifyUniverseConfig modifyConfig = new ModifyUniverseConfig(ybService.getClient(hostPorts),
                                                                   taskParams().universeUUID,
                                                                   taskParams().numReplicas,
                                                                   taskParams().blacklistNodes);
      modifyConfig.doCall();
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }

  // TODO: in future, AbstractModifyMasterClusterConfig.run() should have retries.
  public static class ModifyUniverseConfig extends AbstractModifyMasterClusterConfig {
    UUID universeUUID;
    int numReplicas;
    Set<String> blacklistNodes;

    public ModifyUniverseConfig(YBClient client,
                                UUID universeUUID,
                                int numReplicas,
                                Set<String> blacklistNodes) {
      super(client);
      this.universeUUID = universeUUID;
      this.numReplicas = numReplicas;
      this.blacklistNodes = blacklistNodes;
    }

    @Override
    protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config) {
      Universe universe = Universe.get(universeUUID);

      Master.SysClusterConfigEntryPB.Builder configBuilder =
          Master.SysClusterConfigEntryPB.newBuilder(config);

      // Clear the placement info, as it is no longer valid.
      Master.PlacementInfoPB.Builder placementInfoPB =
          configBuilder.clearReplicationInfo().getReplicationInfoBuilder().getLiveReplicasBuilder();
      // Set the replication factor to the number of masters.
      placementInfoPB.setNumReplicas(numReplicas);
      LOG.info("Starting modify config with {} masters.", numReplicas);
      // Create the placement info for the universe.
      PlacementInfo placementInfo = universe.getUniverseDetails().placementInfo;
      for (PlacementCloud placementCloud : placementInfo.cloudList) {
        Provider cloud = Provider.find.byId(placementCloud.uuid);
        for (PlacementRegion placementRegion : placementCloud.regionList) {
          Region region = Region.get(placementRegion.uuid);
          for (PlacementAZ placementAz : placementRegion.azList) {
            AvailabilityZone az = AvailabilityZone.find.byId(placementAz.uuid);
            // Create the cloud info object.
            WireProtocol.CloudInfoPB.Builder ccb = WireProtocol.CloudInfoPB.newBuilder();
            ccb.setPlacementCloud(placementCloud.code)
               .setPlacementRegion(region.code)
               .setPlacementZone(az.code);

            Master.PlacementBlockPB.Builder pbb = Master.PlacementBlockPB.newBuilder();
            // Set the cloud info.
            pbb.setCloudInfo(ccb);
            // Set the minimum number of replicas in this PlacementAZ.
            pbb.setMinNumReplicas(placementAz.replicationFactor);
            placementInfoPB.addPlacementBlocks(pbb);
          }
        }
      }
      placementInfoPB.build();

      // Add in any black listed nodes of tablet servers.
      if (blacklistNodes != null) {
        Master.BlacklistPB.Builder blacklistBuilder = configBuilder.getServerBlacklistBuilder();
        for (String nodeName : blacklistNodes) {
          NodeDetails node = universe.getNode(nodeName);
          if (node.isTserver) {
            blacklistBuilder.addHosts(ProtobufHelper.hostAndPortToPB(
                HostAndPort.fromParts(node.cloudInfo.private_ip, node.tserverRpcPort)));
          }
        }
        blacklistBuilder.build();
      }

      Master.SysClusterConfigEntryPB newConfig = configBuilder.build();
      LOG.info("Updating cluster config, old config = [{}], new config = [{}]",
               config.toString(), newConfig.toString());
      return newConfig;
    }
  }
}
