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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.CancellationException;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Waits until the master leader reports the tservers of the given nodes as live, so that the next
// tasks, like creating a table, do not lack tserver resources.
@Slf4j
public class WaitForTServerHeartBeats extends UniverseTaskBase {

  // Timeout when the tservers have not heartbeaten to the master leader.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  // Time to wait (in millisec) during each iteration of check.
  private static final int WAIT_EACH_ATTEMPT_MS = 250;

  @Inject
  protected WaitForTServerHeartBeats(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for tserver heartbeat wait task.
  public static class Params extends UniverseTaskParams {
    // Names of the nodes whose tservers must be live; null means every tserver in the universe.
    public Set<String> nodeNames;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Set<String> expected = getExpectedNodeNames(universe);
    log.info(
        "Running {}: masterAddresses={}, nodes={}.",
        getName(),
        universe.getMasterAddresses(),
        expected);
    Set<String> missing = new HashSet<>(expected);
    if (missing.isEmpty()) {
      return;
    }
    int numTries = 1;
    long start = System.currentTimeMillis();
    while (true) {
      try {
        Set<String> liveHosts =
            getLiveTabletServers(universe).stream()
                .map(server -> server.getPrivateAddress().getHost())
                .collect(Collectors.toSet());
        missing.removeIf(name -> isLive(universe.getNode(name), liveHosts));
        if (missing.isEmpty()) {
          return;
        }
        log.info(
            "Waiting for the tservers on {} to heartbeat to master leader; retrying after {}ms."
                + " Tried {} times.",
            missing,
            WAIT_EACH_ATTEMPT_MS,
            numTries);
        waitFor(Duration.ofMillis(getSleepMultiplier() * WAIT_EACH_ATTEMPT_MS));
        numTries++;
      } catch (CancellationException e) {
        throw e;
      } catch (Exception e) {
        log.warn("{}: ignoring error '{}'.", getName(), e.getMessage());
      }
      if (System.currentTimeMillis() >= start + TIMEOUT_SERVER_WAIT_MS) {
        throw new RuntimeException(getName() + " timed out waiting for the tservers on " + missing);
      }
    }
  }

  // Matches from the node's side: a live host maps to every node with that IP, not only the first.
  private static boolean isLive(NodeDetails node, Set<String> liveHosts) {
    return node != null && Util.getNodeIps(node).stream().anyMatch(liveHosts::contains);
  }

  private Set<String> getExpectedNodeNames(Universe universe) {
    if (taskParams().nodeNames == null) {
      return universe.getTServers().stream()
          .map(NodeDetails::getNodeName)
          .collect(Collectors.toSet());
    }
    return taskParams().nodeNames;
  }
}
