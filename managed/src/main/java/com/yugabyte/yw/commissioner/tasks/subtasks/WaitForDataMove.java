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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import javax.inject.Inject;
import lombok.Builder;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@Slf4j
public class WaitForDataMove extends UniverseTaskBase {

  // Time to wait (in millisec) during each iteration of load move completion check.
  private static final int WAIT_EACH_ATTEMPT_MS = 100;

  // Number of response errors to tolerate.
  private static final int MAX_ERRORS_TO_IGNORE = 128;

  // Log after these many iterations
  private static final int LOG_EVERY_NUM_ITERS = 100;

  @Inject
  protected WaitForDataMove(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams {}

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    int numErrors = 0;
    double percent = 0;
    int numIters = 0;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    log.info("Running {} on masterAddress = {}.", getName(), masterAddresses);
    try (YBClient client =
        ybService.getClient(masterAddresses, universe.getCertificateNodetoNode())) {
      log.info("Leader Master UUID={}.", client.getLeaderMasterUUID());
      String taskUUIDString = getTaskUUID().toString();
      while (percent < 100) {
        waitFor(Duration.ofMillis(getSleepMultiplier() * WAIT_EACH_ATTEMPT_MS));
        GetLoadMovePercentResponse response = client.getLoadMoveCompletion();
        if (response.hasError()) {
          log.warn("{} response has error {}.", getName(), response.errorMessage());
          numErrors++;
          // If there are more than the threshold of response errors, bail out.
          if (numErrors >= MAX_ERRORS_TO_IGNORE) {
            String errorMsg = getName() + ": hit too many errors during data move completion wait.";
            log.error(errorMsg);
            throw new RuntimeException(errorMsg);
          }
          continue;
        }

        // Tablet movement.
        TabletProgressInfo tabletProgressInfo =
            TabletProgressInfo.builder()
                .numTotalTablets(response.getTotal())
                .numRemainingTablets(response.getRemaining())
                .numMovedTablets(response.getTotal() - response.getRemaining())
                .tabletMovePercentCompleted(response.getPercentCompleted())
                .tabletMoveElapsedMilliSecs(response.getElapsedMillis())
                .build();
        JsonNode tabletProgress = Json.toJson(tabletProgressInfo);
        if (log.isTraceEnabled()) {
          log.trace("Adding tablet movement data {} in the task cache", tabletProgress.asText());
        }
        // Update the cache.
        getTaskCache().put(taskUUIDString, tabletProgress);
        percent = response.getPercentCompleted();
        numIters++;
        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          log.info("Info: iters={}, percent={}, numErrors={}.", numIters, percent, numErrors);
        }
      }
      // Verify that no tablets are assigned to the blacklisted tserver after the data-move
      // completes.
      verifyNoTabletsOnBlacklistedTservers(universe);
    } catch (Exception e) {
      log.error("{} hit error {}.", getName(), e.getMessage(), e);
      Throwables.propagate(e);
    }
  }

  @Builder
  @Data
  private static class TabletProgressInfo {
    // Ignoring field tsUUID. An example value is "tsUUID" : "YB Master - 10.9.194.23:7100"
    private long numTotalTablets;
    private long numRemainingTablets;
    private long numMovedTablets;
    private double tabletMovePercentCompleted;
    private long tabletMoveElapsedMilliSecs;
  }
}
