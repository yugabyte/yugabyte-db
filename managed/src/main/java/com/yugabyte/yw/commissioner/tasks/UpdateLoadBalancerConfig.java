package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ClusterAZ;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Slf4j
public class UpdateLoadBalancerConfig extends UniverseDefinitionTaskBase {

  private UniverseDefinitionTaskParams newUniverseDetails;

  @Inject
  protected UpdateLoadBalancerConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    log.info("Started {} task for univ uuid={}", getName(), taskParams().getUniverseUUID());
    Universe universe = getUniverse();
    try {
      // Update the universe DB with the changes to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                // Get expected LB config
                Map<LoadBalancerPlacement, LoadBalancerConfig> newLbMap =
                    createLoadBalancerMap(taskParams(), null, null, null);
                // Get current LB config
                Map<ClusterAZ, String> currentLBs = taskParams().existingLBs;
                // Add old LBs to newLbMap so that nodes are removed from old LBs
                compareLBs(taskParams(), newLbMap, currentLBs);
                // Create manage load balancer task
                createManageLoadBalancerTasks(newLbMap);
              });

      // Update universe to new LB config
      Universe.UniverseUpdater updater =
          u -> {
            updateUniverse(u, taskParams());
          };
      saveUniverseDetails(updater);

      getRunnableTask().runSubTasks();
    } catch (Exception e) {
      log.error("Task Errored out with: " + e);
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate(universe.getUniverseUUID());
    }
  }

  private void compareLBs(
      UniverseDefinitionTaskParams taskParams,
      Map<LoadBalancerPlacement, LoadBalancerConfig> newLbMap,
      Map<ClusterAZ, String> currLBs) {
    for (Map.Entry<ClusterAZ, String> currLB : currLBs.entrySet()) {
      ClusterAZ clusterAZ = currLB.getKey();
      String lbName = currLB.getValue();
      UniverseDefinitionTaskParams.Cluster cluster =
          taskParams.getClusterByUuid(clusterAZ.getClusterUUID());
      UUID providerUUID = cluster.placementInfo.cloudList.get(0).uuid;
      AvailabilityZone az = clusterAZ.getAz();

      LoadBalancerPlacement lbPlacement =
          new LoadBalancerPlacement(providerUUID, az.getRegion().getCode(), lbName);
      newLbMap.computeIfAbsent(lbPlacement, v -> new LoadBalancerConfig(lbName));
    }
  }

  private void updateUniverse(Universe universe, UniverseDefinitionTaskParams taskParams) {
    try {
      // If this universe is not being edited, fail the request.
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      if (!universeDetails.updateInProgress) {
        String errMsg = "UserUniverse " + taskParams().getUniverseUUID() + " is not being edited.";
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }

      // Update new load balancer config
      Map<UUID, Map<UUID, PlacementInfo.PlacementAZ>> clusterPlacementMap =
          PlacementInfoUtil.getPlacementAZMapPerCluster(universe);
      for (Map.Entry<UUID, Map<UUID, PlacementInfo.PlacementAZ>> clusterEntry :
          clusterPlacementMap.entrySet()) {
        // Update cluster userintent
        UUID clusterUUID = clusterEntry.getKey();
        UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(clusterUUID);
        UniverseDefinitionTaskParams.Cluster expCluster = taskParams.getClusterByUuid(clusterUUID);
        cluster.userIntent.enableLB = expCluster.userIntent.enableLB;
        // Update load balancer FQDN
        List<PlacementInfo.PlacementRegion> regionList =
            cluster.placementInfo.cloudList.get(0).regionList;
        List<PlacementInfo.PlacementRegion> expRegionList =
            expCluster.placementInfo.cloudList.get(0).regionList;
        for (PlacementInfo.PlacementRegion region : regionList) {
          region.lbFQDN = getLbFQDN(expRegionList, region.uuid, region.lbFQDN);
        }
        // Update load balancer name
        Map<UUID, PlacementInfo.PlacementAZ> expPlacement =
            PlacementInfoUtil.getPlacementAZMap(expCluster.placementInfo);
        for (Map.Entry<UUID, PlacementInfo.PlacementAZ> azEntry :
            clusterEntry.getValue().entrySet()) {
          UUID azUUID = azEntry.getKey();
          PlacementInfo.PlacementAZ placementAZ = azEntry.getValue();
          placementAZ.lbName = expPlacement.getOrDefault(azUUID, placementAZ).lbName;
        }
        universe.setUniverseDetails(universeDetails);
      }
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }

  private String getLbFQDN(
      List<PlacementInfo.PlacementRegion> regionList, UUID regionUUID, String currLbFQDN) {
    List<PlacementInfo.PlacementRegion> list =
        regionList.stream().filter(r -> r.uuid == regionUUID).collect(Collectors.toList());
    if (CollectionUtils.isNotEmpty(list)) {
      return list.get(0).lbFQDN;
    }
    return currLbFQDN;
  }
}
