// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.time.Duration;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.lang.InterruptedException;
import java.util.concurrent.ExecutionException;

import play.libs.Json;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.models.Universe;

import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class PerfAdvisorScheduler {

  private final PlatformScheduler platformScheduler;

  private final ExecutorService threadPool;

  @Inject play.Configuration appConfig;

  @Inject
  public PerfAdvisorScheduler(PlatformScheduler platformScheduler) {
    this.platformScheduler = platformScheduler;
    this.threadPool = Executors.newCachedThreadPool();
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofMinutes(appConfig.getLong("yb.perf_advisor.scheduler_interval")),
        this::scheduleRunner);
  }

  void scheduleRunner() {
    log.info("Running Perf Advisor Scheduler");
    int defaultUniBatchSize = appConfig.getInt("yb.perf_advisor.universe_batch_size");
    try {
      Set<UUID> uuidList = Universe.getAllUUIDs();
      Iterator<UUID> iter = uuidList.iterator();
      Set<UUID> subset = new HashSet<UUID>();
      while (iter.hasNext()) {
        subset.add(iter.next());
        if (subset.size() >= defaultUniBatchSize) {
          this.batchRun(subset);
          subset.clear();
        }
      }
      if (!subset.isEmpty()) {
        this.batchRun(subset);
      }
    } catch (Exception e) {
      log.error("Error running perf advisor scheduled run", e);
    }
  }

  private void batchRun(Set<UUID> univUuidSet) {
    Set<Future<JsonNode>> futures = new HashSet<>();
    for (Universe u : Universe.getAllWithoutResources(univUuidSet)) {
      // Check status of universe
      // TODO: Check other details before adding universe
      if (!u.getUniverseDetails().updateInProgress) {
        Future<JsonNode> future =
            threadPool.submit(
                () -> {
                  // Add code to run perf advisor with universe parameters
                  // perfAdvisorClient.runPerfAdvisor(otherParams, listOfRecommendationTypes, etc);
                  return (JsonNode) Json.newObject();
                });
        futures.add(future);
      }
    }
    for (Future<JsonNode> future : futures) {
      try {
        JsonNode response = future.get();
        // Store recommendation information
      } catch (InterruptedException | ExecutionException e) {
        log.error("Error running perf advisor data retrieval", e);
      }
    }
  }
}
