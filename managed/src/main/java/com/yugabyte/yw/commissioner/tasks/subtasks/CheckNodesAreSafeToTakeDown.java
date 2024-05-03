// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AreNodesSafeToTakeDownResponse;
import org.yb.client.MasterErrorException;
import org.yb.client.YBClient;

@Slf4j
public class CheckNodesAreSafeToTakeDown extends ServerSubTaskBase {

  private static final int INITIAL_DELAY_MS = 10;
  private static final int MAX_DELAY_MS = 5000;

  private static final int MAX_ERRORS_TO_IGNORE = 5;

  @Inject
  protected CheckNodesAreSafeToTakeDown(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    public Collection<NodeDetails> masters;
    public Collection<NodeDetails> tservers;
    // whether we need to check nodes one-by-one or all the nodes simultaneously
    public boolean isRolling = true;
    public String targetSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (!confGetter.getConfForScope(universe, UniverseConfKeys.useNodesAreSafeToTakeDown)) {
      log.debug("Skipping check nodes are safe to take down (disabled)");
      return;
    }
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor == 1) {
      log.debug("Skipping check nodes are safe to take down for RF1");
      return;
    }
    if (taskParams().targetSoftwareVersion != null
        && !isApiSupported(taskParams().targetSoftwareVersion)) {
      log.debug("API is not supported for target version {}", taskParams().targetSoftwareVersion);
      return;
    }

    Set<NodeDetails> allNodes = new HashSet<>(taskParams().masters);
    allNodes.addAll(taskParams().tservers);
    for (NodeDetails node : allNodes) {
      UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
      if (!isApiSupported(cluster.userIntent.ybSoftwareVersion)) {
        log.debug(
            "API is not supported for current version {}", cluster.userIntent.ybSoftwareVersion);
        return;
      }
    }

    // Max threshold for follower lag.
    long maxAcceptableFollowerLagMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagMaxThreshold).toMillis();

    long maxTimeoutMs =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout)
            .toMillis();

    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);

    try (YBClient ybClient = getClient()) {
      AtomicInteger errorCnt = new AtomicInteger();
      List<String> lastErrors = new ArrayList<>();
      List<Pair<Collection<String>, Collection<String>>> ipsList = splitIps(universe, cloudEnabled);
      boolean result =
          doWithExponentialTimeout(
              INITIAL_DELAY_MS,
              MAX_DELAY_MS,
              maxTimeoutMs,
              () -> {
                try {
                  lastErrors.clear();
                  for (Pair<Collection<String>, Collection<String>> ips : ipsList) {
                    String currentNodes = "";
                    if (!ips.getFirst().isEmpty()) {
                      currentNodes += "MASTERS: " + ips.getFirst();
                    }
                    if (!ips.getSecond().isEmpty()) {
                      currentNodes += "TSERVERS: " + ips.getSecond();
                    }
                    try {
                      AreNodesSafeToTakeDownResponse resp =
                          ybClient.areNodesSafeToTakeDown(
                              new HashSet<>(ips.getFirst()),
                              new HashSet<>(ips.getSecond()),
                              maxAcceptableFollowerLagMs);

                      if (!resp.isSucessful()) {
                        lastErrors.add(currentNodes + " have a problem: " + resp.getErrorMessage());
                      }
                    } catch (MasterErrorException me) {
                      lastErrors.add(currentNodes + " have a problem: " + me.getMessage());
                    }
                  }
                  log.debug("Last errors: {}", lastErrors);
                  return lastErrors.isEmpty();
                } catch (Exception e) {
                  if (e.getMessage().contains("invalid method name")) {
                    log.error("This db version doesn't support method AreNodesSafeToTakeDown");
                    return true;
                  }
                  if (errorCnt.incrementAndGet() > MAX_ERRORS_TO_IGNORE) {
                    throw new RuntimeException(
                        "Exceeded max errors (" + MAX_ERRORS_TO_IGNORE + ")", e);
                  }
                  log.debug("Error count {}", errorCnt.get());
                  return false;
                }
              });
      if (!result) {
        String runtimeConfigInfo =
            "If temporary unavailability is acceptable, you can briefly "
                + " disable the runtime config "
                + UniverseConfKeys.useNodesAreSafeToTakeDown.getKey()
                + " and retry this operation.";
        if (!lastErrors.isEmpty()) {
          throw new RuntimeException(
              "Aborting because this operation can potentially take down"
                  + " a majority of copies of some tablets (CheckNodesAreSafeToTakeDown). "
                  + runtimeConfigInfo
                  + " Error details: "
                  + lastErrors.stream().collect(Collectors.joining(",")));
        } else {
          throw new RuntimeException(
              "Failed to execute availability check (CheckNodesAreSafeToTakeDown). "
                  + runtimeConfigInfo);
        }
      }

    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private List<Pair<Collection<String>, Collection<String>>> splitIps(
      Universe universe, boolean cloudEnabled) {
    List<Pair<Collection<String>, Collection<String>>> result = new ArrayList<>();
    if (!taskParams().isRolling) {
      result.add(
          new Pair<>(
              extractIps(universe, taskParams().masters, cloudEnabled),
              extractIps(universe, taskParams().tservers, cloudEnabled)));
    } else {
      for (NodeDetails master : taskParams().masters) {
        result.add(
            new Pair<>(
                Collections.singletonList(getIp(universe, master, cloudEnabled)),
                Collections.emptyList()));
      }
      for (NodeDetails tserver : taskParams().tservers) {
        result.add(
            new Pair<>(
                Collections.emptyList(),
                Collections.singletonList(getIp(universe, tserver, cloudEnabled))));
      }
    }
    return result;
  }

  private Collection<String> extractIps(
      Universe universe, Collection<NodeDetails> nodes, boolean cloudEnabled) {
    return nodes.stream().map(n -> getIp(universe, n, cloudEnabled)).collect(Collectors.toList());
  }

  private String getIp(Universe universe, NodeDetails nodeDetails, boolean cloudEnabled) {
    if (GFlagsUtil.isUseSecondaryIP(universe, nodeDetails, cloudEnabled)) {
      return nodeDetails.cloudInfo.secondary_private_ip;
    }
    return nodeDetails.cloudInfo.private_ip;
  }

  public static boolean isApiSupported(String dbVersion) {
    return CommonUtils.isReleaseBetween("2.20.3.0-b8", "2.21.0.0-b0", dbVersion)
        || CommonUtils.isReleaseEqualOrAfter("2.21.0.0-b190", dbVersion);
  }
}
