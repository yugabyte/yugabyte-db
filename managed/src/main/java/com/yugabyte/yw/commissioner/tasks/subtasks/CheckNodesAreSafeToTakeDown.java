// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
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
    public String targetSoftwareVersion;
    public Collection<UpgradeTaskBase.MastersAndTservers> nodesToCheck;
    public boolean fallbackToSingleSplits;
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
    if (taskParams().nodesToCheck.isEmpty()) {
      return;
    }

    if (!isApiSupported(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
      log.debug(
          "API is not supported for current version {}",
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      return;
    }

    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);

    if (cloudEnabled) {
      log.debug("Skipping check for ybm");
      return;
    }

    int maxSplit =
        taskParams().nodesToCheck.stream()
            .mapToInt(mt -> Math.max(mt.tserversList.size(), mt.mastersList.size()))
            .max()
            .getAsInt();

    Collection<UpgradeTaskBase.MastersAndTservers> singleSplits =
        toSingleSplits(taskParams().nodesToCheck);

    try (YBClient ybClient = getClient()) {
      List<String> lastErrors = new ArrayList<>();
      boolean result =
          checkNodes(ybClient, universe, taskParams().nodesToCheck, lastErrors, cloudEnabled);
      if (!result
          && taskParams().fallbackToSingleSplits
          && singleSplits.size() > taskParams().nodesToCheck.size()) {
        log.debug(
            "Failed are nodes safe pre-check with batch size {}, switching to single", maxSplit);
        lastErrors.clear();
        getTaskCache().putObject(UpgradeTaskBase.SPLIT_FALLBACK, new RollMaxBatchSize());
        result = checkNodes(ybClient, universe, singleSplits, lastErrors, cloudEnabled);
      }
      if (!result) {
        String runtimeConfigInfo =
            "If temporary unavailability is acceptable, you can briefly "
                + " disable the runtime config "
                + UniverseConfKeys.useNodesAreSafeToTakeDown.getKey()
                + " and retry this operation.";
        if (!lastErrors.isEmpty()) {
          fail(
              "Aborting because this operation can potentially take down"
                  + " a majority of copies of some tablets (CheckNodesAreSafeToTakeDown). "
                  + runtimeConfigInfo
                  + " Error details: "
                  + String.join(",", lastErrors));
        } else {
          fail(
              "Failed to execute availability check (CheckNodesAreSafeToTakeDown). "
                  + runtimeConfigInfo);
        }
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private Collection<UpgradeTaskBase.MastersAndTservers> toSingleSplits(
      Collection<UpgradeTaskBase.MastersAndTservers> nodesToCheck) {
    return nodesToCheck.stream()
        .flatMap(mnt -> mnt.splitToSingle().stream())
        .collect(Collectors.toList());
  }

  private boolean checkNodes(
      YBClient ybClient,
      Universe universe,
      Collection<UpgradeTaskBase.MastersAndTservers> nodesToCheck,
      List<String> lastErrors,
      boolean cloudEnabled) {
    AtomicInteger errorCnt = new AtomicInteger();
    // Max threshold for follower lag.
    long maxAcceptableFollowerLagMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagMaxThreshold).toMillis();

    long maxTimeoutMs =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout)
            .toMillis();

    if (taskParams().isRunOnlyPrechecks()) {
      // Only single try.
      maxTimeoutMs = 1;
    }

    return doWithExponentialTimeout(
        INITIAL_DELAY_MS,
        MAX_DELAY_MS,
        maxTimeoutMs,
        () -> {
          try {
            lastErrors.clear();
            for (UpgradeTaskBase.MastersAndTservers ips : nodesToCheck) {
              Set<String> masterIps = extractIps(universe, ips.mastersList, cloudEnabled);
              Set<String> tserverIps = extractIps(universe, ips.tserversList, cloudEnabled);
              String currentNodes = "";
              if (!masterIps.isEmpty()) {
                currentNodes += "MASTERS: " + masterIps;
              }
              if (!tserverIps.isEmpty()) {
                if (!currentNodes.isEmpty()) {
                  currentNodes += ",";
                }
                currentNodes += "TSERVERS: " + tserverIps;
              }
              try {
                AreNodesSafeToTakeDownResponse resp =
                    ybClient.areNodesSafeToTakeDown(
                        masterIps, tserverIps, maxAcceptableFollowerLagMs);

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
              throw new RuntimeException("Exceeded max errors (" + MAX_ERRORS_TO_IGNORE + ")", e);
            }
            log.debug("Error count {}", errorCnt.get());
            return false;
          }
        });
  }

  private void fail(String message) {
    message +=
        ". This check could be disabled by 'yb.checks.nodes_safe_to_take_down.enabled' config";
    throw new RuntimeException(message);
  }

  private Set<String> extractIps(
      Universe universe, Collection<NodeDetails> nodes, boolean cloudEnabled) {
    return nodes.stream()
        .map(n -> Util.getIpToUse(universe, n.getNodeName(), cloudEnabled))
        .filter(ip -> ip != null)
        .collect(Collectors.toSet());
  }

  private String getIp(Universe universe, NodeDetails nodeDetails, boolean cloudEnabled) {
    // For K8s the NodeDetails are only populated with nodeName, so need to fetch details
    // from Universe, which works for both K8s and VMs.
    NodeDetails nodeInUniverse = universe.getNode(nodeDetails.nodeName);
    if (GFlagsUtil.isUseSecondaryIP(universe, nodeInUniverse, cloudEnabled)) {
      return nodeInUniverse.cloudInfo.secondary_private_ip;
    }
    return nodeInUniverse.cloudInfo.private_ip;
  }

  public static boolean isApiSupported(String dbVersion) {
    return CommonUtils.isReleaseBetween("2.20.3.0-b8", "2.21.0.0-b0", dbVersion)
        || CommonUtils.isReleaseEqualOrAfter("2.21.0.0-b190", dbVersion);
  }
}
