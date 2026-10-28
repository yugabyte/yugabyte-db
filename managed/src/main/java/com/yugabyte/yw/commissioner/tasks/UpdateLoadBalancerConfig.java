package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ClusterAZ;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import play.libs.Json;

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
      // Lock the universe but don't freeze it because this task doesn't perform critical updates to
      // universe metadata.
      universe = lockUniverse(-1 /* expectedUniverseVersion */);

      // Get expected LB config
      Map<LoadBalancerPlacement, LoadBalancerConfig> newLbMap =
          createLoadBalancerMap(taskParams(), null, null, null);

      // Get current LB config
      Map<ClusterAZ, String> currentLBs = taskParams().existingLBs;
      // Add old LBs to newLbMap so that nodes are removed from old LBs
      log.info("Original newLbMap {}, currentLBs {}", Json.toJson(newLbMap), currentLBs);
      compareLBs(taskParams(), newLbMap, currentLBs);
      log.info("After comparing, newLbMap {}, currentLBs {}", Json.toJson(newLbMap), currentLBs);
      // Create manage load balancer task
      createManageLoadBalancerTasks(newLbMap);

      // Update universe to new LB config
      Universe.UniverseUpdater updater =
          u -> {
            updateUniverse(u, taskParams());
          };
      saveUniverseDetails(updater);
      // After the save: the gflags follow the new load balancer settings.
      createRebindProxiesTasks();

      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      getRunnableTask().runSubTasks();
    } catch (Exception e) {
      log.error("Task Errored out with: " + e);
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate(universe.getUniverseUUID());
    }
  }

  /**
   * Restarts, one at a time, the tservers whose YSQL or YCQL proxy doesn't listen where its gflags
   * now say: GFlagsUtil binds the proxies of a GCP cluster with a load balancer to 0.0.0.0. A node
   * whose running proxies already match is skipped.
   */
  private void createRebindProxiesTasks() {
    Universe universe = getUniverse();
    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);
    Set<ServerType> tserver = EnumSet.of(ServerType.TSERVER);
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      if (!GFlagsUtil.listensOnLoadBalancerAddress(cluster)) {
        continue;
      }
      for (NodeDetails node : universe.getNodesInCluster(cluster.uuid)) {
        if (!node.isTserver
            || node.state != NodeState.Live
            || !needsProxyRebind(universe, cluster, node, cloudEnabled)) {
          continue;
        }
        List<NodeDetails> nodes = Collections.singletonList(node);
        stopProcessesOnNodes(
            nodes,
            tserver,
            false /* removeMasterFromQuorum */,
            false /* deconfigure */,
            false /* flushTablets */,
            false /* ignoreStopError */,
            SubTaskGroupType.StoppingNodeProcesses);
        createGFlagsOverrideTasks(nodes, ServerType.TSERVER);
        startProcessesOnNode(
            node,
            tserver,
            SubTaskGroupType.StartingNodeProcesses,
            false /* addMasterToQuorum */,
            true /* wasStopped */,
            true /* waitForServerReady */);
      }
    }
  }

  private boolean needsProxyRebind(
      Universe universe, Cluster cluster, NodeDetails node, boolean cloudEnabled) {
    Map<String, String> target =
        GFlagsUtil.calculateFinalGFlags(
            node,
            getAnsibleConfigureServerParams(
                cluster.userIntent,
                node,
                ServerType.TSERVER,
                UpgradeTaskType.GFlags,
                UpgradeTaskSubType.None),
            cluster.userIntent,
            universe,
            GFlagsUtil.getGFlagsForNode(
                node, ServerType.TSERVER, cluster, universe.getUniverseDetails().clusters),
            config,
            confGetter);
    Map<String, String> running =
        GFlagsUtil.getActualGFlags(
            node,
            ServerType.TSERVER,
            universe,
            true /* inMemory */,
            nodeUniverseManager,
            nodeUIApiHelper,
            cloudEnabled);
    // Unknown: a restart is safe, a skip may leave the load balancer unable to connect.
    if (running == null) {
      return true;
    }
    return Stream.of(GFlagsUtil.PSQL_PROXY_BIND_ADDRESS, GFlagsUtil.CSQL_PROXY_BIND_ADDRESS)
        .filter(target::containsKey)
        .anyMatch(flag -> !Objects.equals(target.get(flag), running.get(flag)));
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
        regionList.stream().filter(r -> regionUUID.equals(r.uuid)).collect(Collectors.toList());
    if (CollectionUtils.isNotEmpty(list)) {
      return list.get(0).lbFQDN;
    }
    return currLbFQDN;
  }
}
