// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Collections;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.util.TabletServerInfo;
import play.libs.Json;

/**
 * This precheck verifies that on a node removal we will not end up with tablets that have nowhere
 * to go.
 */
@Slf4j
public class CheckTabletsMovementAvailableForNode extends BaseTabletsMovementCheck {

  @Inject
  protected CheckTabletsMovementAvailableForNode(BaseTaskDependencies baskTaskDependencies) {
    super(baskTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static class Params extends ServerSubTaskParams {
    public String nodeName;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (currentNode == null) {
      throw new IllegalStateException("Node " + taskParams().nodeName + " is not found!");
    }
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(currentNode.placementUuid);
    PlacementInfo placementInfo = cluster.getOverallPlacement();
    // Making copy
    placementInfo = Json.fromJson(Json.toJson(placementInfo), PlacementInfo.class);
    PlacementInfo.PlacementAZ targetAZ = placementInfo.findByAZUUID(currentNode.azUuid);
    if (targetAZ == null) {
      throw new IllegalStateException(
          "Zone " + currentNode.azUuid + " is not found in cluster placement");
    }
    targetAZ.numNodesInAZ -= 1;

    checkNodesRemoval(
        Collections.singletonList(currentNode),
        universe,
        placementInfo,
        Collections.emptySet(),
        true);
  }

  @Override
  protected String getErrorMessage() {
    return "Cannot remove node";
  }

  @Override
  protected int getAvailableTserversPerZone(
      NodeDetails currentNode, Universe universe, PlacementInfo targetPlacementInfo) {
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    UniverseDefinitionTaskParams.Cluster curCluster =
        universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid);
    if (CommonUtils.isReleaseBefore(CommonUtils.MIN_LIVE_TABLET_SERVERS_RELEASE, softwareVersion)) {
      log.debug("ListLiveTabletServers is not supported for {} version", softwareVersion);
      // Just counting based on the current information.
      return super.getAvailableTserversPerZone(currentNode, universe, targetPlacementInfo);
    }
    // We do not get isActive() tservers due to new masters starting up changing
    //   nodeStates to not-active node states which will cause retry to fail.
    // Note: On master leader failover, if a tserver was already down, it will not be reported as a
    //    "live" tserver even though it has been less than
    //    "follower_unavailable_considered_failed_sec" secs since the tserver was down. This is
    //    fine because we do not take into account the current node and if it is not the current
    //    node that is down we may prematurely fail, which is expected.
    List<TabletServerInfo> liveTabletServers = getLiveTabletServers(universe);
    List<TabletServerInfo> tserversActiveInAZExcludingCurrentNode =
        liveTabletServers.stream()
            .filter(
                tserverInfo ->
                    currentNode.cloudInfo.cloud.equals(tserverInfo.getCloudInfo().getCloud())
                        && currentNode.cloudInfo.region.equals(
                            tserverInfo.getCloudInfo().getRegion())
                        && currentNode.cloudInfo.az.equals(tserverInfo.getCloudInfo().getZone())
                        && curCluster.uuid.equals(tserverInfo.getPlacementUuid())
                        && !currentNode.cloudInfo.private_ip.equals(
                            tserverInfo.getPrivateAddress().getHost()))
            .collect(Collectors.toList());
    log.debug(
        "Live tablet servers {}",
        tserversActiveInAZExcludingCurrentNode.stream()
            .map(t -> t.getPrivateAddress().getHost())
            .collect(Collectors.toSet()));
    return tserversActiveInAZExcludingCurrentNode.size();
  }
}
