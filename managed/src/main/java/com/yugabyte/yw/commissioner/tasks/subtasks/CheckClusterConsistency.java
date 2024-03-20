// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;

@Slf4j
public class CheckClusterConsistency extends ServerSubTaskBase {
  private static long MAX_WAIT_TIME_MS = TimeUnit.MINUTES.toMillis(2);
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected CheckClusterConsistency(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    public boolean skipToBeRemoved = true;
  }

  @Override
  protected CheckClusterConsistency.Params taskParams() {
    return (CheckClusterConsistency.Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (!confGetter.getConfForScope(universe, UniverseConfKeys.verifyClusterStateBeforeTask)) {
      log.debug("Skipping check cluster consistency");
      return;
    }
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    log.info("Running {} on masterAddress = {}.", getName(), masterAddresses);
    Set<String> errors;
    try (YBClient ybClient = ybService.getClient(masterAddresses, certificate)) {
      errors = doCheckServers(ybClient, universe);
    } catch (Exception e) {
      throw new RuntimeException("Failed to compare current state", e);
    }
    if (!errors.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Current cluster state doesn't correspond to desired state: "
              + errors.stream().collect(Collectors.joining(",")));
    }
  }

  private Set<String> doCheckServers(YBClient ybClient, Universe universe) {
    Set<String> errors = new HashSet<>();
    doWithConstTimeout(
        DELAY_BETWEEN_RETRIES_MS,
        MAX_WAIT_TIME_MS,
        () -> {
          errors.clear();
          try {
            ListMastersResponse listMastersResponse = ybClient.listMasters();
            Set<String> masterIps =
                listMastersResponse.getMasters().stream()
                    .map(m -> m.getHost())
                    .collect(Collectors.toSet());

            errors.addAll(checkServers(masterIps, universe, UniverseTaskBase.ServerType.MASTER));

            boolean hasLeader =
                listMastersResponse.getMasters().stream()
                    .filter(m -> m.isLeader())
                    .findFirst()
                    .isPresent();
            if (!hasLeader) {
              errors.add("No master leader!");
            }
            ListLiveTabletServersResponse liveTabletServersResponse =
                ybClient.listLiveTabletServers();
            Set<String> tserverIps =
                liveTabletServersResponse.getTabletServers().stream()
                    .map(ts -> ts.getPrivateAddress().getHost())
                    .collect(Collectors.toSet());
            errors.addAll(checkServers(tserverIps, universe, UniverseTaskBase.ServerType.TSERVER));
          } catch (Exception e) {
            throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
          }
          if (!errors.isEmpty()) {
            log.warn("Check is not passed, current errors are: {}", errors);
          }
          return errors.isEmpty();
        });
    return errors;
  }

  private Set<String> checkServers(
      Set<String> currentIPs, Universe universe, UniverseTaskBase.ServerType serverType) {
    Set<String> errors = new HashSet<>();
    List<NodeDetails> neededServers =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(this::isInActiveState)
            .filter(
                n -> serverType == UniverseTaskBase.ServerType.MASTER ? n.isMaster : n.isTserver)
            .collect(Collectors.toList());

    Set<String> expectedIPs =
        neededServers.stream().map(n -> n.cloudInfo.private_ip).collect(Collectors.toSet());

    log.debug("Checking {}: expected {} actual {}", serverType, expectedIPs, currentIPs);

    for (String currentIP : currentIPs) {
      if (!expectedIPs.remove(currentIP)) {
        NodeDetails nodeDetails = universe.getNodeByPrivateIP(currentIP);
        if (nodeDetails != null) {
          errors.add(
              String.format(
                  "Unexpected %s: %s, node %s is not marked as %s",
                  serverType.name(), currentIP, nodeDetails.getNodeName(), serverType.name()));
        } else {
          errors.add(
              String.format(
                  "Unexpected %s: %s, node with such ip is not present in cluster",
                  serverType.name(), currentIP));
        }
      }
    }
    return errors;
  }

  private boolean isInActiveState(NodeDetails nodeDetails) {
    return nodeDetails.state != NodeDetails.NodeState.ToBeAdded
        && nodeDetails.state != NodeDetails.NodeState.Stopped
        && nodeDetails.state != NodeDetails.NodeState.Stopping
        && nodeDetails.state != NodeDetails.NodeState.Removed
        && nodeDetails.state != NodeDetails.NodeState.Removing
        && nodeDetails.state != NodeDetails.NodeState.Terminated
        && nodeDetails.state != NodeDetails.NodeState.Terminating
        && nodeDetails.state != NodeDetails.NodeState.Decommissioned;
  }
}
