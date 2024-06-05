// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Iterables;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformUniverseNodeConfig;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.UniversePerfAdvisorRun;
import com.yugabyte.yw.models.UniversePerfAdvisorRun.State;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.queries.QueryHelper;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.yb.perf_advisor.configs.PerfAdvisorScriptConfig;
import org.yb.perf_advisor.configs.UniverseConfig;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationType;
import org.yb.perf_advisor.services.generation.PlatformPerfAdvisor;

@Singleton
@Slf4j
public class PerfAdvisorScheduler {

  public static final String DATABASE_DRIVER_PARAM = "db.perf_advisor.driver";

  private static final String PERF_ADVISOR_RUN_IN_PROGRESS = "Perf advisor run in progress";

  private final PlatformScheduler platformScheduler;

  private final ExecutorService threadPool;

  private final List<RecommendationType> DB_QUERY_RECOMMENDATION_TYPES =
      Arrays.asList(RecommendationType.UNUSED_INDEX, RecommendationType.RANGE_SHARDING);

  private final PlatformPerfAdvisor platformPerfAdvisor;

  private final QueryHelper queryHelper;

  /* Simple universe locking mechanism to prevent parallel universe runs */
  private final Map<UUID, Boolean> universesLock = new ConcurrentHashMap<>();

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
    if (!configFactory
        .staticApplicationConf()
        .getString(DATABASE_DRIVER_PARAM)
        .equals("org.postgresql.Driver")) {
      log.debug(
          "Skipping perf advisor scheduler initialization for tests"
              + " or other non-postgresql environment");
      return;
    }
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
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping perf advisor scheduler for follower platform");
      return;
    }
    log.info("Running Perf Advisor Scheduler");
    int defaultUniBatchSize =
        configFactory.staticApplicationConf().getInt("yb.perf_advisor.universe_batch_size");

    try {
      Map<Long, Customer> customerMap =
          Customer.getAll().stream()
              .collect(Collectors.toMap(Customer::getId, Function.identity()));
      Set<UUID> uuidList = Universe.getAllUUIDs();
      for (List<UUID> batch : Iterables.partition(uuidList, defaultUniBatchSize)) {
        this.batchRun(batch, customerMap);
      }
    } catch (Exception e) {
      log.error("Error running perf advisor scheduled run", e);
    }
  }

  private void batchRun(List<UUID> univUuidSet, Map<Long, Customer> customerMap) {
    for (Universe universe : Universe.getAllWithoutResources(univUuidSet)) {
      run(customerMap.get(universe.getCustomerId()), universe, true);
    }
  }

  private RunResult run(Customer customer, Universe universe, boolean scheduled) {
    // Check status of universe
    if (universe.getUniverseDetails().isUniverseBusyByTask()) {
      return RunResult.builder()
          .failureReason("Universe task, which may affect performance, is in progress")
          .build();
    }
    if (universesLock.containsKey(universe.getUniverseUUID())) {
      UniversePerfAdvisorRun currentRun =
          UniversePerfAdvisorRun.getLastRun(customer.getUuid(), universe.getUniverseUUID(), false)
              .orElse(null);
      return RunResult.builder()
          .failureReason(PERF_ADVISOR_RUN_IN_PROGRESS)
          .activeRun(currentRun)
          .build();
    }

    Config universeConfig = configFactory.forUniverse(universe);
    boolean enabled = universeConfig.getBoolean(UniverseConfKeys.perfAdvisorEnabled.getKey());
    if (scheduled && !enabled) {
      return RunResult.builder().failureReason("Perf advisor disabled for universe").build();
    }
    if (scheduled) {
      Optional<UniversePerfAdvisorRun> lastScheduledRun =
          UniversePerfAdvisorRun.getLastRun(customer.getUuid(), universe.getUniverseUUID(), true);
      Long lastRun =
          lastScheduledRun
              .map(UniversePerfAdvisorRun::getScheduleTime)
              .map(Date::getTime)
              .orElse(null);
      int runFrequencyMins =
          universeConfig.getInt(UniverseConfKeys.perfAdvisorUniverseFrequencyMins.getKey());
      if (lastRun != null
          && Instant.now()
              .isBefore(Instant.ofEpochMilli(lastRun).plus(runFrequencyMins, ChronoUnit.MINUTES))) {
        // Need to wait before subsequent run
        return RunResult.builder().failureReason("Skipped due to last run time").build();
      }
    }

    List<PlatformUniverseNodeConfig> universeNodeConfigList = new ArrayList<>();
    for (NodeDetails details : universe.getNodes()) {
      if (!details.isTserver) {
        continue;
      }
      if (details.state.equals(NodeDetails.NodeState.Live)) {
        PlatformUniverseNodeConfig nodeConfig =
            new PlatformUniverseNodeConfig(details, universe, ShellProcessContext.DEFAULT);
        universeNodeConfigList.add(nodeConfig);
      }
    }

    if (universeNodeConfigList.isEmpty()) {
      log.warn(
          String.format(
              "Universe %s node config list has no TServers! Skipping..",
              universe.getUniverseUUID()));
      return RunResult.builder().failureReason("No Live nodes found").build();
    }

    RunResult.RunResultBuilder result =
        RunResult.builder().failureReason(PERF_ADVISOR_RUN_IN_PROGRESS);
    universesLock.computeIfAbsent(
        universe.getUniverseUUID(),
        (k) -> {
          UniversePerfAdvisorRun run =
              UniversePerfAdvisorRun.create(
                  customer.getUuid(), universe.getUniverseUUID(), !scheduled);
          try {
            threadPool.submit(
                () ->
                    runPerfAdvisor(
                        customer, universe, universeConfig, universeNodeConfigList, run));
            result.started(true).activeRun(run).failureReason(null);
            return true;
          } catch (Exception e) {
            log.error(
                "Failed to submit perf advisor task for universe " + universe.getUniverseUUID(), e);
            run.setEndTime(new Date()).setState(State.FAILED).save();
            throw e;
          }
        });
    if (result.failureReason != null && result.failureReason.equals(PERF_ADVISOR_RUN_IN_PROGRESS)) {
      // If we ended up here we're in a race condition where other run started faster.
      // Let's return it in run result.
      UniversePerfAdvisorRun currentRun =
          UniversePerfAdvisorRun.getLastRun(customer.getUuid(), universe.getUniverseUUID(), false)
              .orElse(null);
      result.activeRun(currentRun);
    }
    return result.build();
  }

  private void runPerfAdvisor(
      Customer customer,
      Universe universe,
      Config universeConfig,
      List<PlatformUniverseNodeConfig> universeNodeConfigList,
      UniversePerfAdvisorRun run) {
    try {
      run.setStartTime(new Date()).setState(State.RUNNING).save();
      List<RecommendationType> dbQueryRecommendationTypes = DB_QUERY_RECOMMENDATION_TYPES;
      JsonNode databaseNamesResult = queryHelper.listDatabaseNames(universe);
      List<String> databases = new ArrayList<>();
      if (databaseNamesResult.has("error")) {
        String errorMessage = databaseNamesResult.get("error").toString();
        log.error(
            "Failed to get database names for universe "
                + universe.getUniverseUUID()
                + ": "
                + errorMessage);
        // We'll not retrieve any recommendation types from DB nodes in these case - just pass
        // empty recommendation types list to leave existing recommendations active.
        dbQueryRecommendationTypes = Collections.emptyList();
      } else {
        Iterator<JsonNode> queryIterator = databaseNamesResult.get("result").elements();
        while (queryIterator.hasNext()) {
          databases.add(queryIterator.next().get("datname").asText());
        }
      }
      Provider provider =
          Provider.getOrBadRequest(
              UUID.fromString(
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
      NodeDetails tserverNode = CommonUtils.getServerToRunYsqlQuery(universe);
      boolean ysqlAuth =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth;
      boolean tlsClient =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt;
      PerfAdvisorScriptConfig scriptConfig =
          new PerfAdvisorScriptConfig(
              databases,
              NodeManager.YUGABYTE_USER,
              tserverNode.ysqlServerRpcPort,
              dbQueryRecommendationTypes,
              ysqlAuth,
              tlsClient);
      // In K8S environment we may have no permissions to write to YB home dir
      String perfAdvisorScriptDestPath =
          (provider.getCloudCode().equals(CloudType.kubernetes)
                  ? KubernetesTaskBase.K8S_NODE_YW_DATA_DIR
                  : provider.getYbHome())
              + "/bin";
      UniverseConfig uConfig =
          new UniverseConfig(
              customer.getUuid(),
              universe.getUniverseUUID(),
              universeNodeConfigList,
              scriptConfig,
              perfAdvisorScriptDestPath,
              universeConfig);
      platformPerfAdvisor.run(uConfig);
      run.setEndTime(new Date()).setState(State.COMPLETED).save();
    } catch (Exception e) {
      log.error("Failed to run perf advisor for universe " + universe.getUniverseUUID(), e);
      run.setEndTime(new Date()).setState(State.FAILED).save();
    } finally {
      universesLock.remove(universe.getUniverseUUID());
    }
  }

  public RunResult runPerfAdvisor(Customer customer, Universe universe) {
    return run(customer, universe, false);
  }

  @Value
  @Builder
  public static class RunResult {
    @Builder.Default boolean started = false;
    String failureReason;
    UniversePerfAdvisorRun activeRun;
  }
}
