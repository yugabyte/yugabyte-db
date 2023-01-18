// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.time.Duration;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import java.util.UUID;
import java.util.HashMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformUniverseNodeConfig;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.queries.QueryHelper;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.typesafe.config.Config;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationType;
import org.yb.perf_advisor.services.generation.PlatformPerfAdvisor;
import org.yb.perf_advisor.configs.UniverseConfig;
import org.yb.perf_advisor.configs.UniverseNodeConfigInterface;
import org.yb.perf_advisor.configs.PerfAdvisorScriptConfig;

import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class PerfAdvisorScheduler {

  private final PlatformScheduler platformScheduler;

  private final ExecutorService threadPool;

  private final String DEFAULT_NODE_USER = "yugabyte";

  private final List<RecommendationType> DB_QUERY_RECOMMENDATION_TYPES =
      Arrays.asList(RecommendationType.UNUSED_INDEX, RecommendationType.RANGE_SHARDING);

  private PlatformPerfAdvisor platformPerfAdvisor;

  private QueryHelper queryHelper;

  /* Simple universe locking mechanism to prevent parallel universe runs */
  private final ConcurrentHashMap.KeySetView<UUID, Boolean> universesLock =
      ConcurrentHashMap.newKeySet();

  private Config appConfig;

  @Inject
  public PerfAdvisorScheduler(
      Config appConfig,
      PlatformScheduler platformScheduler,
      PlatformPerfAdvisor platformPerfAdvisor,
      QueryHelper queryHelper) {
    this.appConfig = appConfig;
    this.platformPerfAdvisor = platformPerfAdvisor;
    this.platformScheduler = platformScheduler;
    this.queryHelper = queryHelper;
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
      Set<UUID> subset = new HashSet<>();
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
    for (Universe u : Universe.getAllWithoutResources(univUuidSet)) {
      // Check status of universe
      if (u.getUniverseDetails().updateInProgress || universesLock.contains(u.universeUUID)) {
        continue;
      }
      List<UniverseNodeConfigInterface> universeNodeConfigList = new ArrayList<>();
      for (NodeDetails details : u.getNodes()) {
        if (details.state.equals(NodeDetails.NodeState.Live)) {
          PlatformUniverseNodeConfig nodeConfig =
              new PlatformUniverseNodeConfig(details, u, ShellProcessContext.DEFAULT);
          universeNodeConfigList.add(nodeConfig);
        }
      }

      if (universeNodeConfigList.isEmpty()) {
        log.warn(
            String.format("Universe %s node config list is empty! Skipping..", u.universeUUID));
        continue;
      }

      JsonNode result = queryHelper.listDatabaseNames(u);
      List<String> databases = new ArrayList<>();
      Iterator<JsonNode> queryIterator = result.get("result").elements();
      while (queryIterator.hasNext()) {
        databases.add(queryIterator.next().get("datname").asText());
      }
      Provider provider =
          Provider.get(
              UUID.fromString(u.getUniverseDetails().getPrimaryCluster().userIntent.provider));
      NodeDetails tserverNode = CommonUtils.getServerToRunYsqlQuery(u);
      String databaseHost = tserverNode.cloudInfo.private_ip;
      boolean ysqlAuth = u.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth;
      boolean tlsClient =
          u.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt;
      PerfAdvisorScriptConfig scriptConfig =
          new PerfAdvisorScriptConfig(
              databases,
              DEFAULT_NODE_USER,
              databaseHost,
              tserverNode.ysqlServerRpcPort,
              DB_QUERY_RECOMMENDATION_TYPES,
              ysqlAuth,
              tlsClient);
      universesLock.add(u.universeUUID);
      Future<Void> future =
          (Future<Void>)
              threadPool.submit(
                  () -> {
                    try {
                      UniverseConfig uConfig =
                          new UniverseConfig(
                              u.universeUUID,
                              universeNodeConfigList,
                              scriptConfig,
                              provider.getYbHome() + "/bin");
                      // Add code to run perf advisor with universe parameters
                      platformPerfAdvisor.run(uConfig);
                    } finally {
                      universesLock.remove(u.universeUUID);
                    }
                  });
    }
  }
}
