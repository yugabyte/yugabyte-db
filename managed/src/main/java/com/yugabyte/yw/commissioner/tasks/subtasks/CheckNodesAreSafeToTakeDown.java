// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.base.Throwables;
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
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AreNodesSafeToTakeDownResponse;
import org.yb.client.MasterErrorException;
import org.yb.client.YBClient;

@Slf4j
public class CheckNodesAreSafeToTakeDown extends ServerSubTaskBase {

  private static final int INITIAL_DELAY_MS = 500;
  private static final int MAX_DELAY_MS = 16000;

  private static final int MAX_ERRORS_TO_IGNORE = 5;
  private static final long MAX_WAIT_FOR_VALIDATION = TimeUnit.SECONDS.toMillis(40);

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

    int maxSplit =
        taskParams().nodesToCheck.stream()
            .mapToInt(mt -> Math.max(mt.tserversList.size(), mt.mastersList.size()))
            .max()
            .getAsInt();

    Collection<UpgradeTaskBase.MastersAndTservers> singleSplits =
        toSingleSplits(taskParams().nodesToCheck);

    Integer maxPoolSize =
        confGetter.getConfForScope(universe, UniverseConfKeys.nodesAreSafeToTakeDownParallelism);

    int poolSize = Math.min(maxPoolSize, taskParams().nodesToCheck.size());

    ScheduledExecutorService executor = Executors.newScheduledThreadPool(poolSize);

    try (YBClient ybClient = getClient()) {
      List<String> lastErrors = new ArrayList<>();
      boolean result =
          checkNodes(
              ybClient, universe, taskParams().nodesToCheck, lastErrors, cloudEnabled, executor);
      if (!result
          && taskParams().fallbackToSingleSplits
          && singleSplits.size() > taskParams().nodesToCheck.size()) {
        log.debug(
            "Failed are nodes safe pre-check with batch size {}, switching to single", maxSplit);
        lastErrors.clear();
        getTaskCache().putObject(UpgradeTaskBase.SPLIT_FALLBACK, new RollMaxBatchSize());
        result = checkNodes(ybClient, universe, singleSplits, lastErrors, cloudEnabled, executor);
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
      Throwables.throwIfUnchecked(e);
      throw new RuntimeException(e);
    } finally {
      executor.shutdownNow();
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
      boolean cloudEnabled,
      ScheduledExecutorService executor) {
    // Max threshold for follower lag.
    long maxAcceptableFollowerLagMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagMaxThreshold).toMillis();

    long maxTimeoutMs;
    if (taskParams().isRunOnlyPrechecks()) {
      maxTimeoutMs = MAX_WAIT_FOR_VALIDATION;
    } else {
      maxTimeoutMs =
          confGetter
              .getConfForScope(universe, UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout)
              .toMillis();
    }
    List<CheckBatch> checkBatches =
        nodesToCheck.stream()
            .map(mnt -> new CheckBatch(universe, mnt, cloudEnabled))
            .collect(Collectors.toList());

    AtomicInteger errorCnt = new AtomicInteger();
    CountDownLatch countDownLatch = new CountDownLatch(checkBatches.size());
    long startTime = System.currentTimeMillis();
    try {
      Map<CheckBatch, Runnable> runnableRefs = new ConcurrentHashMap<>();
      for (CheckBatch checkBatch : checkBatches) {
        Runnable runnable =
            () -> {
              if (errorCnt.get() >= MAX_ERRORS_TO_IGNORE) {
                log.debug("Already has {} errors, skipping", errorCnt.get());
                countDownLatch.countDown();
                return;
              }
              boolean reschedule = false;
              try {
                doCheck(ybClient, checkBatch, maxAcceptableFollowerLagMs);
                reschedule = checkBatch.errorStr != null;
              } catch (Exception e) {
                if (errorCnt.incrementAndGet() >= MAX_ERRORS_TO_IGNORE) {
                  log.debug("Too many errors: {}, failing ", errorCnt.get());
                  checkBatch.errorStr =
                      String.format(
                          "Too many errors: %d. Last error: %s", errorCnt.get(), e.getMessage());
                } else {
                  reschedule = true;
                }
              }
              reschedule = reschedule && !taskParams().isRunOnlyPrechecks();
              if (reschedule) {
                long delay =
                    Util.getExponentialBackoffDelayMs(
                        INITIAL_DELAY_MS, MAX_DELAY_MS, checkBatch.iterationNumber.get());
                if (System.currentTimeMillis() + delay < startTime + maxTimeoutMs) {
                  executor.schedule(runnableRefs.get(checkBatch), delay, TimeUnit.MILLISECONDS);
                } else {
                  reschedule = false;
                }
              }
              if (!reschedule) {
                log.debug("countdown for {}, error {}", checkBatch.nodesStr, checkBatch.errorStr);
                checkBatch.finished = true;
                countDownLatch.countDown();
              }
              log.debug("{}/{} checks remaining", countDownLatch.getCount(), checkBatches.size());
            };
        runnableRefs.put(checkBatch, runnable);
        executor.schedule(
            runnable, (long) (Math.random() * INITIAL_DELAY_MS), TimeUnit.MILLISECONDS);
      }
      countDownLatch.await(maxTimeoutMs, TimeUnit.MILLISECONDS);
    } catch (InterruptedException e) {
      log.error("Interrupted", e);
    }
    Set<String> pendingIps = new HashSet<>();
    checkBatches.stream()
        .filter(cb -> !cb.finished && cb.errorStr == null)
        .forEach(
            cb -> {
              pendingIps.addAll(cb.masterIps);
              pendingIps.addAll(cb.tserverIps);
            });
    if (countDownLatch.getCount() > 0 && pendingIps.size() > 0) {
      throw new RuntimeException(
          "Timed out while waiting for all checks to complete. Pending ips: " + pendingIps);
    }
    for (CheckBatch checkBatch : checkBatches) {
      if (checkBatch.errorStr != null) {
        lastErrors.add(checkBatch.errorStr);
      }
    }
    return lastErrors.isEmpty() && errorCnt.get() < MAX_ERRORS_TO_IGNORE;
  }

  private void doCheck(YBClient ybClient, CheckBatch checkBatch, long maxAcceptableFollowerLagMs) {
    String errorStr = null;
    try {
      AreNodesSafeToTakeDownResponse resp =
          ybClient.areNodesSafeToTakeDown(
              checkBatch.masterIps, checkBatch.tserverIps, maxAcceptableFollowerLagMs);
      if (!resp.isSucessful()) {
        errorStr = checkBatch.nodesStr + " have a problem: " + resp.getErrorMessage();
      }
    } catch (Exception e) {
      if (e.getMessage().contains("invalid method name")) {
        log.error("This db version doesn't support method AreNodesSafeToTakeDown");
        errorStr = null;
      } else if (e instanceof MasterErrorException) {
        errorStr = checkBatch.nodesStr + " have a problem: " + e.getMessage();
      } else {
        throw new RuntimeException(e);
      }
    }
    checkBatch.errorStr = errorStr;
    checkBatch.iterationNumber.incrementAndGet();
  }

  private class CheckBatch {
    final Set<String> masterIps;
    final Set<String> tserverIps;
    final String nodesStr;
    final AtomicInteger iterationNumber = new AtomicInteger();
    volatile String errorStr;
    volatile boolean finished;

    private CheckBatch(
        Universe universe, UpgradeTaskBase.MastersAndTservers target, boolean cloudEnabled) {
      masterIps = extractIps(universe, target.mastersList, cloudEnabled);
      tserverIps = extractIps(universe, target.tserversList, cloudEnabled);
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
      nodesStr = currentNodes;
    }
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
