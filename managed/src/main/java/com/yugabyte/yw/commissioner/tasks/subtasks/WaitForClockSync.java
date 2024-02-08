/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.base.Stopwatch;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.concurrent.CancellationException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class WaitForClockSync extends NodeTaskBase {

  private final Duration RETRY_WAIT_TIME = Duration.ofSeconds(10);

  private final NodeUniverseManager nodeUniverseManager;

  private static final Pattern lastOffsetRegexPattern =
      Pattern.compile("Last offset\\s*:\\s*([+-]?\\d+\\.\\d+)");

  @Inject
  protected WaitForClockSync(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public static class Params extends NodeTaskParams {
    // The name of the node must be stored in nodeName field.

    // Max acceptable clock skew in nano-seconds.
    public long acceptableClockSkewNs;

    // Timeout before making the subtask failed.
    public long subtaskTimeoutMs;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(nodeName=%s,acceptableClockSkewNs=%s,timeoutMs=%s)",
        super.getName(),
        taskParams().nodeName,
        taskParams().acceptableClockSkewNs,
        taskParams().subtaskTimeoutMs);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      throw new IllegalArgumentException(
          "Node "
              + taskParams().nodeName
              + " not found in universe "
              + taskParams().getUniverseUUID());
    }
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    if (cluster.userIntent.providerType == Common.CloudType.local) {
      log.info("Skipping sync for local provider");
      return;
    }

    // It ensures that the chrony service has started, and the last system clock offset is applied
    // such that the correction it needs is less than the time specified in
    // acceptable clock skew specified in the task params.
    waitUsingChronycWaitSync(universe, node);

    // It ensures the last system clock offset from the NTP server is less than
    // acceptable clock skew specified in the task params.
    waitUsingChronycTracking(universe, node);

    log.info("Completed {}", getName());
  }

  private void waitUsingChronycWaitSync(Universe universe, NodeDetails node) {
    long numberOfRetries =
        Math.round((double) taskParams().subtaskTimeoutMs / RETRY_WAIT_TIME.toMillis());
    if (numberOfRetries < 1) {
      // If numberOfRetries is zero the waitsync subcommand never times out. We set it to `1`
      // because it happens only when subtaskTimeoutMs is very small, meaning that the user wants
      // to return as soon as possible.
      numberOfRetries = 1;
    }
    ShellResponse waitsyncShellResponse =
        nodeUniverseManager.runCommand(
            node,
            universe,
            ImmutableList.of(
                "chronyc",
                "waitsync",
                String.format("%d", numberOfRetries) /* max-tries */,
                String.format(
                    "%.9f",
                    taskParams().acceptableClockSkewNs / Math.pow(10, 9)) /* max-correction */,
                "0" /* max-skew: 0 to ignore this */,
                String.format(
                    "%.9f", RETRY_WAIT_TIME.toMillis() / Math.pow(10, 3)) /* interval */));

    String waitsyncOutput = waitsyncShellResponse.extractRunCommandOutput();
    String waitsyncLastTry = waitsyncOutput.substring(waitsyncOutput.lastIndexOf("\n") + 1);
    log.debug("chronyc waitsync output is {}", waitsyncOutput);
    if (waitsyncShellResponse.getCode() == ShellResponse.ERROR_CODE_SUCCESS) {
      log.info("System clock is synchronized with the NTP server by chrony: {}", waitsyncLastTry);
    } else {
      String errorMessage =
          String.format(
              "System clock synchronization failed with the NTP server by chrony: %s",
              waitsyncLastTry);
      log.error("{} hit error : {}", getName(), errorMessage);
      throw new RuntimeException(errorMessage);
    }
  }

  private void waitUsingChronycTracking(Universe universe, NodeDetails node) {
    Stopwatch stopwatch = Stopwatch.createStarted();
    Duration subtaskElapsedTime;
    Duration subtaskTimeout = Duration.ofMillis(taskParams().subtaskTimeoutMs);
    int iterationNumber = 0;

    // Loop until clock skew is less than the requested value or timeout.
    while (true) {
      try {
        long currentClockSkewNs = getCurrentClockSkewNs(universe, node);
        if (currentClockSkewNs <= taskParams().acceptableClockSkewNs) {
          log.info(
              "Current clock skew is {}ns which is less than or equal to "
                  + "acceptableClockSkewNs ({}ns) set in the subtask",
              currentClockSkewNs,
              taskParams().acceptableClockSkewNs);
          break;
        }
        subtaskElapsedTime = stopwatch.elapsed();
        if (subtaskElapsedTime.compareTo(subtaskTimeout) > 0) {
          log.warn("Current clock skew is {}ns", currentClockSkewNs);
        } else {
          log.warn(
              "Current clock skew is {}ns; retrying after {}ms...",
              currentClockSkewNs,
              RETRY_WAIT_TIME.toMillis());
        }
      } catch (CancellationException e) {
        throw e;
      } catch (Exception e) {
        log.error(
            "{} hit error : iteration number={}: {}", getName(), iterationNumber, e.getMessage());
        subtaskElapsedTime = stopwatch.elapsed();
        // There will be no retry, so throw the current exception.
        if (subtaskElapsedTime.compareTo(subtaskTimeout) > 0) {
          throw new RuntimeException(e);
        }
      }

      if (subtaskElapsedTime.compareTo(subtaskTimeout) > 0) {
        throw new RuntimeException(
            String.format(
                "WaitForClockSync: timing out after retrying %s times for a duration of %sms "
                    + "which is more than subtaskTimeout (%sms)",
                iterationNumber, subtaskElapsedTime.toMillis(), subtaskTimeout.toMillis()));
      }

      waitFor(RETRY_WAIT_TIME);
      iterationNumber++;
    }
  }

  private long getCurrentClockSkewNs(Universe universe, NodeDetails node) {
    String chronyTrackingOutput =
        nodeUniverseManager
            .runCommand(node, universe, ImmutableList.of("chronyc", "tracking"))
            .processErrors("Running `chronyc tracking` failed")
            .extractRunCommandOutput();

    log.debug("chronyTrackingOutput is {}", chronyTrackingOutput);
    /* Sample output:
    Reference ID    : A9FEA9FE (metadata.google.internal)
    Stratum         : 3
    Ref time (UTC)  : Mon Jun 12 16:18:24 2023
    System time     : 0.000000003 seconds slow of NTP time
    Last offset     : +0.000019514 seconds
    RMS offset      : 0.000011283 seconds
    Frequency       : 99.154 ppm slow
    Residual freq   : +0.009 ppm
    Skew            : 0.106 ppm
    Root delay      : 0.000162946 seconds
    Root dispersion : 0.000101734 seconds
    Update interval : 32.3 seconds
    Leap status     : Normal
     */
    Matcher m = lastOffsetRegexPattern.matcher(chronyTrackingOutput);
    if (!m.find()) {
      throw new RuntimeException("No match found for lastOffsetRegexPattern");
    }

    double lastOffsetValueNs = Double.parseDouble(m.group(1)) * 1_000_000_000L;
    return Math.abs((long) lastOffsetValueNs);
  }
}
