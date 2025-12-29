/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.BiConsumer;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonNet.CloudInfoPB;
import org.yb.CommonNet.PlacementBlockPB;
import org.yb.CommonNet.PlacementInfoPB;
import org.yb.CommonNet.ReplicationInfoPB;
import org.yb.client.AbstractModifyMasterClusterConfig;
import org.yb.client.ProtobufHelper;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

@Slf4j
public class UpdatePlacementInfo extends UniverseTaskBase {

  @Inject
  protected UpdatePlacementInfo(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for placement info update task.
  public static class Params extends UniverseTaskParams {
    // If present, then we intend to decommission these nodes.
    public Set<String> blacklistNodes = null;
    public List<Cluster> targetClusterStates;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "'("
        + taskParams().getUniverseUUID()
        + " "
        + taskParams().blacklistNodes
        + ")'";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    try (YBClient client = ybService.getUniverseClient(universe)) {
      log.info("Running {}: masterAddresses={}.", getName(), universe.getMasterAddresses());

      ModifyUniverseConfig modifyConfig =
          new ModifyUniverseConfig(
              client,
              taskParams().getUniverseUUID(),
              taskParams().blacklistNodes,
              taskParams().targetClusterStates);
      modifyConfig.doCall();
      Cluster primaryCluster =
          getTargetClusterState(
              universe.getUniverseDetails().getPrimaryCluster(), taskParams().targetClusterStates);
      if (primaryCluster.placementInfo.hasRankOrdering()) {
        Map<Integer, List<CloudInfoPB>> rankings = new HashMap<>();
        ModifyUniverseConfig.mapToCloudInfoPB(
            primaryCluster.placementInfo,
            (az, cloudInfo) -> {
              List<CloudInfoPB> list =
                  rankings.computeIfAbsent(az.leaderPreference, x -> new ArrayList<>());
              list.add(cloudInfo);
            });
        client.setPreferredZones(rankings);
      }
      if (shouldIncrementVersion(taskParams().getUniverseUUID())) {
        universe.incrementVersion();
      }

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }

  // TODO: in future, AbstractModifyMasterClusterConfig.run() should have retries.
  public static class ModifyUniverseConfig extends AbstractModifyMasterClusterConfig {
    final UUID universeUUID;
    final Set<String> blacklistNodes;
    final List<Cluster> targetClusterStates;

    public ModifyUniverseConfig(
        YBClient client,
        UUID universeUUID,
        Set<String> blacklistNodes,
        List<Cluster> targetClusterStates) {
      super(client);
      this.universeUUID = universeUUID;
      this.blacklistNodes = blacklistNodes;
      this.targetClusterStates = targetClusterStates;
    }

    public static void generatePlacementInfoPB(
        PlacementInfoPB.Builder placementInfoPB, Cluster cluster) {
      PlacementInfo placementInfo = cluster.getOverallPlacement();
      mapToCloudInfoPB(
          placementInfo,
          (placementAz, ccb) -> {
            PlacementBlockPB.Builder pbb = PlacementBlockPB.newBuilder();
            // Set the cloud info.
            pbb.setCloudInfo(ccb);
            // Set the minimum number of replicas in this PlacementAZ.
            pbb.setMinNumReplicas(placementAz.replicationFactor);
            placementInfoPB.addPlacementBlocks(pbb);
          });
      placementInfoPB.setNumReplicas(cluster.userIntent.replicationFactor);
      placementInfoPB.setPlacementUuid(ByteString.copyFromUtf8(cluster.uuid.toString()));
      placementInfoPB.build();
    }

    public static void mapToCloudInfoPB(
        PlacementInfo placementInfo, BiConsumer<PlacementAZ, CloudInfoPB> callback) {
      for (PlacementCloud placementCloud : placementInfo.cloudList) {
        Provider cloud = Provider.find.byId(placementCloud.uuid);
        for (PlacementRegion placementRegion : placementCloud.regionList) {
          Region region = Region.get(placementRegion.uuid);
          for (PlacementAZ placementAz : placementRegion.azList) {
            AvailabilityZone az = AvailabilityZone.find.byId(placementAz.uuid);
            // Create the cloud info object.
            CloudInfoPB.Builder ccb = CloudInfoPB.newBuilder();
            ccb.setPlacementCloud(placementCloud.code)
                .setPlacementRegion(region.getCode())
                .setPlacementZone(az.getCode());
            callback.accept(placementAz, ccb.build());
          }
        }
      }
    }

    public static void addAffinitizedPlacements(
        ReplicationInfoPB.Builder replicationInfoPB, PlacementInfo placementInfo) {
      mapToCloudInfoPB(
          placementInfo,
          (placementAz, cloudInfo) -> {
            if (placementAz.isAffinitized) {
              replicationInfoPB.addAffinitizedLeaders(cloudInfo);
            }
          });
    }

    @Override
    public CatalogEntityInfo.SysClusterConfigEntryPB modifyConfig(
        CatalogEntityInfo.SysClusterConfigEntryPB config) {
      Universe universe = Universe.getOrBadRequest(universeUUID);

      CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
          CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder(config);

      // Clear the replication info, as it is no longer valid.
      ReplicationInfoPB.Builder replicationInfoPB =
          configBuilder.clearReplicationInfo().getReplicationInfoBuilder();
      // Build the live replicas from the replication info.
      PlacementInfoPB.Builder placementInfoPB = replicationInfoPB.getLiveReplicasBuilder();
      // Create the placement info for the universe.
      Cluster primaryCluster =
          getTargetClusterState(
              universe.getUniverseDetails().getPrimaryCluster(), targetClusterStates);

      PlacementInfo placementInfo = primaryCluster.getOverallPlacement();
      generatePlacementInfoPB(placementInfoPB, primaryCluster);

      List<Cluster> readOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
      for (Cluster cluster : readOnlyClusters) {
        PlacementInfoPB.Builder placementInfoReadPB = replicationInfoPB.addReadReplicasBuilder();
        generatePlacementInfoPB(
            placementInfoReadPB, getTargetClusterState(cluster, targetClusterStates));
      }
      if (!placementInfo.hasRankOrdering()) {
        addAffinitizedPlacements(replicationInfoPB, placementInfo);
      }
      replicationInfoPB.build();

      // Add in any black listed nodes of tablet servers.
      if (blacklistNodes != null) {
        CatalogEntityInfo.BlacklistPB.Builder blacklistBuilder =
            configBuilder.getServerBlacklistBuilder();
        for (String nodeName : blacklistNodes) {
          NodeDetails node = universe.getNode(nodeName);
          if (node == null) {
            log.info("Skipping non existing hosts");
            continue;
          }
          if (node.cloudInfo.private_ip != null) {
            blacklistBuilder.addHosts(
                ProtobufHelper.hostAndPortToPB(
                    HostAndPort.fromParts(node.cloudInfo.private_ip, node.tserverRpcPort)));
          }
        }
        blacklistBuilder.build();
      }

      CatalogEntityInfo.SysClusterConfigEntryPB newConfig = configBuilder.build();
      log.info(
          "Updating cluster config, old config = [{}], new config = [{}]",
          config.toString(),
          newConfig.toString());
      return newConfig;
    }
  }

  private static Cluster getTargetClusterState(Cluster cluster, List<Cluster> targetClusterStates) {
    if (targetClusterStates != null) {
      return targetClusterStates.stream()
          .filter(c -> c.uuid.equals(cluster.uuid))
          .findFirst()
          .orElse(cluster);
    }
    return cluster;
  }
}
