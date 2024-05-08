// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
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
    public Collection<String> masterIps;
    public Collection<String> tserverIps;
    // whether we need to check nodes one-by-one or all the nodes simultaneously
    public boolean isRolling = true;
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

    // Max threshold for follower lag.
    long maxAcceptableFollowerLagMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagMaxThreshold).toMillis();

    long maxTimeoutMs =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout)
            .toMillis();

    try (YBClient ybClient = getClient()) {
      AtomicInteger errorCnt = new AtomicInteger();
      List<String> lastErrors = new ArrayList<>();
      List<Pair<Collection<String>, Collection<String>>> ipsList = splitIps();
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
        if (!lastErrors.isEmpty()) {
          throw new RuntimeException(
              "Nodes are not safe to take down: "
                  + lastErrors.stream().collect(Collectors.joining(",")));
        } else {
          throw new RuntimeException(
              "Failed to check that nodes are not safe to take down: got "
                  + errorCnt.get()
                  + " errors");
        }
      }

    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private List<Pair<Collection<String>, Collection<String>>> splitIps() {
    List<Pair<Collection<String>, Collection<String>>> result = new ArrayList<>();
    if (!taskParams().isRolling) {
      result.add(new Pair<>(taskParams().masterIps, taskParams().tserverIps));
    } else {
      for (String masterIp : taskParams().masterIps) {
        result.add(new Pair<>(Collections.singletonList(masterIp), Collections.emptyList()));
      }
      for (String tserverIp : taskParams().tserverIps) {
        result.add(new Pair<>(Collections.emptyList(), Collections.singletonList(tserverIp)));
      }
    }
    return result;
  }
}
