// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Iterables;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformUniverseNodeConfig;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.queries.QueryHelper;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.yb.perf_advisor.configs.PerfAdvisorScriptConfig;
import org.yb.perf_advisor.configs.UniverseConfig;
import org.yb.perf_advisor.configs.UniverseNodeConfigInterface;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationType;
import org.yb.perf_advisor.services.generation.PlatformPerfAdvisor;

@Singleton
@Slf4j
public class PerfAdvisorScheduler {

  private final PlatformScheduler platformScheduler;

  private final ExecutorService threadPool;

  private final List<RecommendationType> DB_QUERY_RECOMMENDATION_TYPES =
      Arrays.asList(RecommendationType.UNUSED_INDEX, RecommendationType.RANGE_SHARDING);

  private final PlatformPerfAdvisor platformPerfAdvisor;

  private final QueryHelper queryHelper;

  /* Simple universe locking mechanism to prevent parallel universe runs */
  private final Map<UUID, Boolean> universesLock = new ConcurrentHashMap<>();

  private final Map<UUID, Long> universeLastScheduledRun = new HashMap<>();

  private final SettableRuntimeConfigFactory configFactory;

  @Inject
  public PerfAdvisorScheduler(
      SettableRuntimeConfigFactory settableRuntimeConfigFactory,
      PlatformScheduler platformScheduler,
      PlatformPerfAdvisor platformPerfAdvisor,
      QueryHelper queryHelper) {
    this.configFactory = settableRuntimeConfigFactory;
    this.platformPerfAdvisor = platformPerfAdvisor;
    this.platformScheduler = platformScheduler;
    this.queryHelper = queryHelper;
    this.threadPool = Executors.newCachedThreadPool();
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofMinutes(
            configFactory
                .staticApplicationConf()
                .getLong("yb.perf_advisor.scheduler_interval_mins")),
        this::scheduleRunner);
  }

  void scheduleRunner() {
    log.info("Running Perf Advisor Scheduler");
    int defaultUniBatchSize =
        configFactory.staticApplicationConf().getInt("yb.perf_advisor.universe_batch_size");
    try {
      Set<UUID> uuidList = Universe.getAllUUIDs();
      for (List<UUID> batch : Iterables.partition(uuidList, defaultUniBatchSize)) {
        this.batchRun(batch);
      }
    } catch (Exception e) {
      log.error("Error running perf advisor scheduled run", e);
    }
  }

  private void batchRun(List<UUID> univUuidSet) {
    for (Universe universe : Universe.getAllWithoutResources(univUuidSet)) {
      run(universe, true);
    }
  }

  private RunResult run(Universe universe, boolean scheduled) {
    // Check status of universe
    if (universe.getUniverseDetails().updateInProgress) {
      return RunResult.builder().failureReason("Universe update in progress").build();
    }
    if (universesLock.containsKey(universe.universeUUID)) {
      return RunResult.builder().failureReason("Perf advisor run in progress").build();
    }

    Config universeConfig = configFactory.forUniverse(universe);
    boolean enabled = universeConfig.getBoolean(UniverseConfKeys.perfAdvisorEnabled.getKey());
    if (!enabled) {
      return RunResult.builder().failureReason("Perf advisor disabled for universe").build();
    }
    if (scheduled) {
      Long lastRun = universeLastScheduledRun.get(universe.getUniverseUUID());
      int runFrequencyMins =
          universeConfig.getInt(UniverseConfKeys.perfAdvisorUniverseFrequencyMins.getKey());
      if (lastRun != null
          && Instant.now()
              .isBefore(Instant.ofEpochMilli(lastRun).plus(runFrequencyMins, ChronoUnit.MINUTES))) {
        // Need to wait before subsequent run
        return RunResult.builder().failureReason("Skipped due to last run time").build();
      }
    }

    List<UniverseNodeConfigInterface> universeNodeConfigList = new ArrayList<>();
    for (NodeDetails details : universe.getNodes()) {
      if (details.state.equals(NodeDetails.NodeState.Live)) {
        PlatformUniverseNodeConfig nodeConfig =
            new PlatformUniverseNodeConfig(details, universe, ShellProcessContext.DEFAULT);
        universeNodeConfigList.add(nodeConfig);
      }
    }

    if (universeNodeConfigList.isEmpty()) {
      log.warn(
          String.format(
              "Universe %s node config list is empty! Skipping..", universe.universeUUID));
      return RunResult.builder().failureReason("No Live nodes found").build();
    }

    JsonNode databaseNamesResult = queryHelper.listDatabaseNames(universe);
    List<String> databases = new ArrayList<>();
    Iterator<JsonNode> queryIterator = databaseNamesResult.get("result").elements();
    while (queryIterator.hasNext()) {
      databases.add(queryIterator.next().get("datname").asText());
    }
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    NodeDetails tserverNode = CommonUtils.getServerToRunYsqlQuery(universe);
    String databaseHost = tserverNode.cloudInfo.private_ip;
    boolean ysqlAuth = universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth;
    boolean tlsClient =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt;
    PerfAdvisorScriptConfig scriptConfig =
        new PerfAdvisorScriptConfig(
            databases,
            NodeManager.YUGABYTE_USER,
            databaseHost,
            tserverNode.ysqlServerRpcPort,
            DB_QUERY_RECOMMENDATION_TYPES,
            ysqlAuth,
            tlsClient);
    Customer customer = Customer.get(universe.customerId);
    RunResult.RunResultBuilder result =
        RunResult.builder().failureReason("Perf advisor run in progress");
    universesLock.computeIfAbsent(
        universe.universeUUID,
        (k) -> {
          threadPool.submit(
              () -> {
                try {
                  if (scheduled) {
                    universeLastScheduledRun.put(
                        universe.getUniverseUUID(), System.currentTimeMillis());
                  }
                  UniverseConfig uConfig =
                      new UniverseConfig(
                          customer.getUuid(),
                          universe.universeUUID,
                          universeNodeConfigList,
                          scriptConfig,
                          provider.getYbHome() + "/bin",
                          universeConfig);
                  platformPerfAdvisor.run(uConfig);
                } finally {
                  universesLock.remove(universe.universeUUID);
                }
              });
          result.started(true).failureReason(null);
          return true;
        });
    return result.build();
  }

  public RunResult runPerfAdvisor(Universe universe) {
    return run(universe, false);
  }

  @Value
  @Builder
  public static class RunResult {
    @Builder.Default boolean started = false;
    String failureReason;
  }
}
