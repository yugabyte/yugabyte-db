// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMasterRaftPeersResponse;
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
    public Set<String> skipMayBeRunning = new HashSet<>();
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
    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);
    if (cloudEnabled) {
      log.debug("Skipping check for ybm");
      return;
    }
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (CommonUtils.isReleaseBefore(CommonUtils.MIN_LIVE_TABLET_SERVERS_RELEASE, softwareVersion)) {
      log.debug("ListLiveTabletServers is not supported for {} version", softwareVersion);
      return;
    }
    String runtimeConfigInfo =
        "Please contact Yugabyte Support. To override"
            + " this check (not recommended), briefly disable the runtime config "
            + UniverseConfKeys.verifyClusterStateBeforeTask.getKey()
            + ".";
    try (YBClient ybClient = ybService.getClient(masterAddresses, certificate)) {
      errors = doCheckServers(ybClient, universe, cloudEnabled);
    } catch (Exception e) {
      throw new RuntimeException(
          "Unable to verify cluster consistency (CheckClusterConsistency). "
              + runtimeConfigInfo
              + " Encountered error: "
              + e);
    }
    if (!errors.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "YBA metadata seems inconsistent with actual universe state. "
              + runtimeConfigInfo
              + " Error list: "
              + errors.stream().collect(Collectors.joining(",")));
    }
  }

  private Set<String> doCheckServers(YBClient ybClient, Universe universe, boolean cloudEnabled) {
    Set<String> errors = new HashSet<>();
    long maxWaitTime = taskParams().isRunOnlyPrechecks() ? 1 : MAX_WAIT_TIME_MS;
    doWithConstTimeout(
        DELAY_BETWEEN_RETRIES_MS,
        maxWaitTime,
        () -> {
          errors.clear();
          try {
            errors.addAll(
                checkCurrentServers(
                        ybClient, universe, taskParams().skipMayBeRunning, false, cloudEnabled)
                    .getErrors());
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

  @Data
  public static class CheckResult {
    final Set<String> unknownMasterIps = new HashSet<>();
    final Set<String> unknownTserverIps = new HashSet<>();
    final Set<String> absentMasterIps = new HashSet<>();
    final Set<String> absentTserverIps = new HashSet<>();
    boolean noMasterLeader;
    final List<String> errors = new ArrayList<>();
    long checkTimestamp = System.currentTimeMillis();
  }

  public static CheckResult checkCurrentServers(
      YBClient ybClient,
      Universe universe,
      @Nullable Set<String> skipMaybeRunning,
      boolean strict,
      boolean cloudEnabled)
      throws Exception {
    CheckResult checkResult = new CheckResult();
    if (ybClient.getLeaderMasterHostAndPort() == null) {
      checkResult.noMasterLeader = true;
      checkResult.errors.add("No master leader!");
      return checkResult;
    }
    ListMasterRaftPeersResponse listMastersResponse = ybClient.listMasterRaftPeers();
    Set<String> masterIps =
        listMastersResponse.getPeersList().stream()
            .flatMap(p -> p.getLastKnownPrivateIps().stream())
            .map(hp -> hp.getHost())
            .collect(Collectors.toSet());

    checkServers(
        masterIps,
        skipMaybeRunning,
        universe,
        UniverseTaskBase.ServerType.MASTER,
        strict,
        cloudEnabled,
        checkResult);

    ListLiveTabletServersResponse liveTabletServersResponse = ybClient.listLiveTabletServers();
    Set<String> tserverIps =
        liveTabletServersResponse.getTabletServers().stream()
            .map(ts -> ts.getPrivateAddress().getHost())
            .collect(Collectors.toSet());
    checkServers(
        tserverIps,
        skipMaybeRunning,
        universe,
        UniverseTaskBase.ServerType.TSERVER,
        strict,
        cloudEnabled,
        checkResult);
    checkResult.checkTimestamp = System.currentTimeMillis();
    return checkResult;
  }

  private static void checkServers(
      Set<String> currentIPs,
      Set<String> skipMaybeRunningNodes,
      Universe universe,
      UniverseTaskBase.ServerType serverType,
      boolean strict,
      boolean cloudEnabled,
      CheckResult checkResult) {
    List<NodeDetails> neededServers =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(CheckClusterConsistency::isInActiveState)
            .filter(
                n -> serverType == UniverseTaskBase.ServerType.MASTER ? n.isMaster : n.isTserver)
            .collect(Collectors.toList());

    Set<String> expectedIPs =
        neededServers.stream()
            .map(
                n -> {
                  if (GFlagsUtil.isUseSecondaryIP(universe, n, cloudEnabled)) {
                    return n.cloudInfo.secondary_private_ip;
                  } else {
                    return n.cloudInfo.private_ip;
                  }
                })
            .collect(Collectors.toSet());

    log.debug("Checking {}: expected {} actual {}", serverType, expectedIPs, currentIPs);
    Set<String> unknownIps =
        serverType == UniverseTaskBase.ServerType.MASTER
            ? checkResult.getUnknownMasterIps()
            : checkResult.getUnknownTserverIps();

    for (String currentIP : currentIPs) {
      if (!expectedIPs.remove(currentIP)) {
        NodeDetails nodeDetails = universe.getNodeByAnyIP(currentIP);
        if (nodeDetails != null) {
          if (skipMaybeRunningNodes != null
              && skipMaybeRunningNodes.contains(nodeDetails.nodeName)) {
            log.warn(
                "{} is running on {} but not expected, skipping because may be running",
                serverType.name(),
                currentIP);
            continue;
          }
          unknownIps.add(currentIP);
          checkResult
              .getErrors()
              .add(
                  String.format(
                      "Unexpected %s: %s, node %s is not marked as %s",
                      serverType.name(), currentIP, nodeDetails.getNodeName(), serverType.name()));
        } else {
          unknownIps.add(currentIP);
          checkResult
              .getErrors()
              .add(
                  String.format(
                      "Unexpected %s: %s, node with such ip is not present in cluster",
                      serverType.name(), currentIP));
        }
      }
    }
    if (strict) {
      Set<String> absentIps =
          serverType == UniverseTaskBase.ServerType.MASTER
              ? checkResult.getAbsentMasterIps()
              : checkResult.getAbsentTserverIps();
      for (String expectedIP : expectedIPs) {
        absentIps.add(expectedIP);
        checkResult
            .getErrors()
            .add(String.format("Expected, but not present %s: %s", serverType.name(), expectedIP));
      }
    }
  }

  private static boolean isInActiveState(NodeDetails nodeDetails) {
    return nodeDetails.state != NodeDetails.NodeState.ToBeAdded
        && nodeDetails.state != NodeDetails.NodeState.Stopped
        && nodeDetails.state != NodeDetails.NodeState.Stopping
        && nodeDetails.state != NodeDetails.NodeState.Removed
        && nodeDetails.state != NodeDetails.NodeState.Removing
        && nodeDetails.state != NodeDetails.NodeState.Terminating
        && nodeDetails.state != NodeDetails.NodeState.Decommissioned;
  }
}
