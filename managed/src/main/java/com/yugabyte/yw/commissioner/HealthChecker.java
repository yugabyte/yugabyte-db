/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.HealthCheckMetrics.CLOCK_SYNC_CHECK;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.CUSTOM_NODE_METRICS_COLLECTION_METRIC;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.HEALTH_CHECK_METRICS;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.HEALTH_CHECK_METRICS_WITHOUT_STATUS;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.NODE_EXPORTER_CHECK;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.OPENED_FILE_DESCRIPTORS_CHECK;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.UPTIME_CHECK;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.getCountMetricByCheckName;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.getNodeMetrics;
import static com.yugabyte.yw.common.metrics.MetricService.STATUS_OK;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.alerts.MaintenanceService;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HealthCheck.Details;
import com.yugabyte.yw.models.HealthCheck.Details.NodeData;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricSourceKey;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import jakarta.mail.MessagingException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import play.Environment;
import play.inject.ApplicationLifecycle;
import play.libs.Json;

@Singleton
@Slf4j
public class HealthChecker {

  private static final String PYTHON_WRAPPER_OUTPUT_PREFIX =
      "Using virtualenv python executable now.";

  private static final String YB_TSERVER_PROCESS = "yb-tserver";

  static final String SCRIPT_PERMISSIONS = "755";

  private static final String MAX_NUM_THREADS_KEY = "yb.health.max_num_parallel_checks";

  private static final String MAX_NUM_THREADS_NODE_CHECK_KEY =
      "yb.health.max_num_parallel_node_checks";

  private final Environment environment;

  private final Config config;

  // Last time we sent a status update email per customer.
  private final Map<UUID, Long> lastStatusUpdateTimeMap = new HashMap<>();

  // Last time we actually ran the health check script per customer.
  private final Map<UUID, Long> lastCheckTimeMap = new HashMap<>();

  private final PlatformScheduler platformScheduler;

  private final HealthCheckerReport healthCheckerReport;

  private final EmailHelper emailHelper;

  private final MetricService metricService;

  private final RuntimeConfigFactory runtimeConfigFactory;

  private final RuntimeConfGetter confGetter;

  // The thread pool executor for parallelized health checks for multiple universes.
  private final ExecutorService universeExecutor;
  // The thread pool executor for parallelized node checks for multiple universes.
  private final ExecutorService nodeExecutor;

  // A map of all running health checks.
  final Map<UUID, CompletableFuture<Void>> runningHealthChecks = new ConcurrentHashMap<>();

  // We upload health check script to the node only when NodeInfo is updates
  private final Map<Pair<UUID, String>, NodeInfo> uploadedNodeInfo = new ConcurrentHashMap<>();

  private final Set<String> healthScriptMetrics =
      Collections.newSetFromMap(new ConcurrentHashMap<>());

  final ApplicationLifecycle lifecycle;

  private final NodeUniverseManager nodeUniverseManager;

  private final FileHelperService fileHelperService;

  private final MaintenanceService maintenanceService;

  @Inject
  public HealthChecker(
      Environment environment,
      Config config,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler,
      HealthCheckerReport healthCheckerReport,
      EmailHelper emailHelper,
      MetricService metricService,
      RuntimeConfigFactory runtimeConfigFactory,
      RuntimeConfGetter confGetter,
      ApplicationLifecycle lifecycle,
      NodeUniverseManager nodeUniverseManager,
      FileHelperService fileHelperService,
      MaintenanceService maintenanceService) {
    this(
        environment,
        config,
        platformScheduler,
        healthCheckerReport,
        emailHelper,
        metricService,
        runtimeConfigFactory,
        confGetter,
        lifecycle,
        nodeUniverseManager,
        createUniverseExecutor(platformExecutorFactory, runtimeConfigFactory.globalRuntimeConf()),
        createNodeExecutor(platformExecutorFactory, runtimeConfigFactory.globalRuntimeConf()),
        fileHelperService,
        maintenanceService);
  }

  HealthChecker(
      Environment environment,
      Config config,
      PlatformScheduler platformScheduler,
      HealthCheckerReport healthCheckerReport,
      EmailHelper emailHelper,
      MetricService metricService,
      RuntimeConfigFactory runtimeConfigFactory,
      RuntimeConfGetter confGetter,
      ApplicationLifecycle lifecycle,
      NodeUniverseManager nodeUniverseManager,
      ExecutorService universeExecutor,
      ExecutorService nodeExecutor,
      FileHelperService fileHelperService,
      MaintenanceService maintenanceService) {
    this.environment = environment;
    this.config = config;
    this.platformScheduler = platformScheduler;
    this.healthCheckerReport = healthCheckerReport;
    this.emailHelper = emailHelper;
    this.metricService = metricService;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.confGetter = confGetter;
    this.lifecycle = lifecycle;
    this.universeExecutor = universeExecutor;
    this.nodeExecutor = nodeExecutor;
    this.nodeUniverseManager = nodeUniverseManager;
    this.fileHelperService = fileHelperService;
    this.maintenanceService = maintenanceService;
  }

  public void initialize() {
    log.info("Scheduling health checker every {} ms", healthCheckIntervalMs());
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO /* initialDelay */,
        Duration.ofMillis(healthCheckIntervalMs()) /* interval */,
        this::scheduleRunner);
  }

  // The interval at which the checker will run.
  // Can be overridden per customer.
  private long healthCheckIntervalMs() {
    Long interval = config.getLong("yb.health.check_interval_ms");
    return interval == null ? 0 : interval;
  }

  // The interval at which check result will be stored to DB
  // Can be overridden per customer.
  private long healthCheckStoreIntervalMs() {
    Long interval = config.getLong("yb.health.store_interval_ms");
    return interval == null ? 0 : interval;
  }

  // The interval at which to send a status update of all the current universes.
  // Can be overridden per customer.
  private long statusUpdateIntervalMs() {
    Long interval = config.getLong("yb.health.status_interval_ms");
    return interval == null ? 0 : interval;
  }

  /**
   * Process metrics received from the health-check script.
   *
   * @param c Customer.
   * @param u Universe.
   * @param report Health report.
   * @return true if success
   */
  private void processMetrics(Customer c, Universe u, Details report) {

    boolean hasErrors = false;
    // This is hacky, but health check data items only make sense if you know order.
    boolean isMaster = true;
    boolean isInstanceUp = false;
    try {
      List<Metric> metrics = new ArrayList<>();
      Map<PlatformMetrics, Integer> platformMetrics = new HashMap<>();
      Set<String> nodesWithError = new HashSet<>();
      boolean shouldCollectNodeMetrics = false;
      for (NodeData nodeData : report.getData()) {
        String node = nodeData.getNode();
        String checkName = nodeData.getMessage();
        boolean checkResult = nodeData.getHasError();
        if (checkResult) {
          nodesWithError.add(node);
        }
        hasErrors = (!nodeData.getMetricsOnly() && checkResult) || hasErrors;

        // First data node for master and tserver have process field with process type.
        if (StringUtils.isNotBlank(nodeData.getProcess())) {
          isMaster = !nodeData.getProcess().equals(YB_TSERVER_PROCESS);
        }
        // Add node metric value if it's present in data node
        List<Details.Metric> nodeMetrics = nodeData.getMetrics();
        List<Metric> nodeCustomMetrics =
            new ArrayList<>(getNodeMetrics(c, u, nodeData, nodeMetrics));
        if (checkName.equals(UPTIME_CHECK)) {
          // No boot time metric means the instance or the whole node is down
          // and this node shouldn't be counted in other error node count metrics.
          isInstanceUp = CollectionUtils.isNotEmpty(nodeCustomMetrics);
        }
        if (checkName.equals(NODE_EXPORTER_CHECK)) {
          shouldCollectNodeMetrics =
              nodeCustomMetrics.stream()
                  .noneMatch(
                      metric ->
                          metric.getName().equals(CUSTOM_NODE_METRICS_COLLECTION_METRIC)
                              && metric.getValue().equals(STATUS_OK));
        }

        // Get per-check error nodes count metric name.
        PlatformMetrics countMetric = getCountMetricByCheckName(checkName, isMaster);

        // Only increase error nodes metric value in case it's instance up check
        // or instance is actually up. Otherwise - most probably node is just down
        // and ssh connection to the node failed during check.
        boolean increaseNodeCount =
            isInstanceUp
                || PlatformMetrics.HEALTH_CHECK_MASTER_DOWN == countMetric
                || PlatformMetrics.HEALTH_CHECK_TSERVER_DOWN == countMetric;
        if (countMetric != null && increaseNodeCount) {
          // checkResult == true -> error -> 1
          int toAppend = checkResult ? 1 : 0;
          platformMetrics.compute(countMetric, (k, v) -> v != null ? v + toAppend : toAppend);
        }
        if (shouldCollectNodeMetrics
            || checkName.equals(OPENED_FILE_DESCRIPTORS_CHECK)
            || checkName.equals(CLOCK_SYNC_CHECK)) {
          // Used FD count metric is always collected through health check as it's not
          // calculated properly from inside the collect_metrics service - it gets service limit
          // instead of user limit for file descriptors
          metrics.addAll(nodeCustomMetrics);
        }

        Metric healthCheckStatusMetric =
            HealthCheckMetrics.buildLegacyNodeMetric(c, u, node, checkName, checkResult ? 1D : 0D);
        metrics.add(healthCheckStatusMetric);
      }

      healthScriptMetrics.addAll(
          metrics.stream().map(Metric::getName).collect(Collectors.toList()));
      metrics.addAll(
          platformMetrics.entrySet().stream()
              .map(e -> buildMetricTemplate(e.getKey(), u).setValue(e.getValue().doubleValue()))
              .collect(Collectors.toList()));
      // Clean all health check metrics for universe before saving current values
      // just in case list of nodes changed between runs.
      MetricFilter toClean =
          metricSourceKeysFilterWithHealthScriptMetrics(c, u, HEALTH_CHECK_METRICS_WITHOUT_STATUS);
      metricService.cleanAndSave(metrics, toClean);

      metricService.setMetric(
          buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODES_WITH_ERRORS, u),
          nodesWithError.size());

      metricService.setOkStatusMetric(
          buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODE_METRICS_STATUS, u));
    } catch (Exception e) {
      log.warn("Failed to convert health check response to prometheus metrics", e);
      metricService.setFailureStatusMetric(
          buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODE_METRICS_STATUS, u));
    }
  }

  private boolean sendEmailReport(
      Customer c,
      Universe u,
      String emailDestinations,
      boolean sendMailAlways,
      boolean reportOnlyErrors,
      Details report) {
    SmtpData smtpData = emailHelper.getSmtpData(c.getUuid());
    if (!StringUtils.isEmpty(emailDestinations)
        && (smtpData != null)
        && (sendMailAlways || report.getHasError())) {
      String subject =
          String.format(
              "%s - <%s> %s", report.getHasError() ? "ERROR" : "OK", c.getTag(), u.getName());

      // LinkedHashMap saves values order.
      JsonNode reportJson = Json.toJson(report);
      Map<String, String> contentMap = new LinkedHashMap<>();
      contentMap.put(
          "text/plain; charset=\"us-ascii\"",
          healthCheckerReport.asPlainText(reportJson, reportOnlyErrors));
      contentMap.put(
          "text/html; charset=\"us-ascii\"",
          healthCheckerReport.asHtml(u, reportJson, reportOnlyErrors));

      try {
        emailHelper.sendEmail(c, subject, emailDestinations, smtpData, contentMap);
      } catch (MessagingException e) {
        log.warn("Health check had the following errors during mailing: " + e.getMessage());
        metricService.setFailureStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, u));
        return false;
      }
    }
    return true;
  }

  @VisibleForTesting
  void scheduleRunner() {
    try {
      if (HighAvailabilityConfig.isFollower()) {
        log.debug("Skipping health check scheduler for follower platform");
        return;
      }
      // TODO(bogdan): This will not be too DB friendly when we go multi-tenant.
      for (Customer c : Customer.getAll()) {
        try {
          checkCustomer(c);
        } catch (Exception ex) {
          log.error("Error running health check scheduler for customer " + c.getUuid(), ex);
        }
      }
    } catch (Exception e) {
      log.error("Error running health check scheduler", e);
    }
  }

  public void checkCustomer(Customer c) {
    // We need an alerting config to do work.
    CustomerConfig config = CustomerConfig.getAlertConfig(c.getUuid());

    boolean shouldSendStatusUpdate = false;
    long storeIntervalMs = healthCheckStoreIntervalMs();
    AlertingData alertingData = null;
    long now = (new Date()).getTime();
    if (config != null && config.getData() != null) {
      alertingData = Json.fromJson(config.getData(), AlertingData.class);
      if (alertingData.checkIntervalMs > 0) {
        storeIntervalMs = alertingData.checkIntervalMs;
      }

      long statusUpdateIntervalMs =
          alertingData.statusUpdateIntervalMs <= 0
              ? statusUpdateIntervalMs()
              : alertingData.statusUpdateIntervalMs;
      shouldSendStatusUpdate =
          (now - statusUpdateIntervalMs) > lastStatusUpdateTimeMap.getOrDefault(c.getUuid(), 0L);
      if (shouldSendStatusUpdate) {
        lastStatusUpdateTimeMap.put(c.getUuid(), now);
      }
    }
    boolean onlyMetrics =
        !shouldSendStatusUpdate
            && ((now - storeIntervalMs) < lastCheckTimeMap.getOrDefault(c.getUuid(), 0L));

    if (!onlyMetrics) {
      lastCheckTimeMap.put(c.getUuid(), now);
    }
    checkAllUniverses(c, alertingData, shouldSendStatusUpdate, onlyMetrics);
  }

  public CompletableFuture<Void> checkSingleUniverse(Customer c, Universe u) {
    if (!confGetter.getConfForScope(u, UniverseConfKeys.enableTriggerAPI)) {
      throw new PlatformServiceException(BAD_REQUEST, "Manual health check is disabled.");
    }
    // We hardcode the parameters here as this is currently a cloud-only feature
    CheckSingleUniverseParams params =
        new CheckSingleUniverseParams(
            u,
            c,
            false /*shouldSendStatusUpdate*/,
            false /*reportOnlyErrors*/,
            false /*onlyMetrics*/,
            null /*destinations*/);

    return runHealthCheck(params, true /* ignoreLastCheck */);
  }

  @AllArgsConstructor
  static class CheckSingleUniverseParams {

    final Universe universe;
    final Customer customer;
    final boolean shouldSendStatusUpdate;
    final boolean reportOnlyErrors;
    final boolean onlyMetrics;
    final String emailDestinations;
  }

  @VisibleForTesting
  Config getRuntimeConfig() {
    return this.runtimeConfigFactory.globalRuntimeConf();
  }

  private static int getThreadpoolParallelism(Config runtimeConfig) {
    return runtimeConfig.getInt(HealthChecker.MAX_NUM_THREADS_KEY);
  }

  @VisibleForTesting
  void checkAllUniverses(
      Customer c, AlertingData alertingData, boolean shouldSendStatusUpdate, boolean onlyMetrics) {

    boolean reportOnlyErrors =
        !shouldSendStatusUpdate && alertingData != null && alertingData.reportOnlyErrors;

    c.getUniverses().stream()
        .map(
            u -> {
              String destinations = getAlertDestinations(u, c);
              return new CheckSingleUniverseParams(
                  u, c, shouldSendStatusUpdate, reportOnlyErrors, onlyMetrics, destinations);
            })
        .forEach(params -> runHealthCheck(params, false /*ignoreLastHealthCheck*/));
  }

  public void cancelHealthCheck(UUID universeUUID) {
    CompletableFuture<Void> lastCheckForUUID = this.runningHealthChecks.get(universeUUID);
    if (lastCheckForUUID == null) {
      return;
    }

    lastCheckForUUID.cancel(true);
  }

  public void markUniverseForReUpload(UUID universeUUID) {
    List<Pair<UUID, String>> universeNodeInfos =
        uploadedNodeInfo.keySet().stream()
            .filter(key -> key.getFirst().equals(universeUUID))
            .collect(Collectors.toList());
    universeNodeInfos.forEach(uploadedNodeInfo::remove);
  }

  public void handleUniverseRemoval(UUID universeUUID) {
    cancelHealthCheck(universeUUID);
    runningHealthChecks.remove(universeUUID);
    List<Pair<UUID, String>> universeNodeInfos =
        uploadedNodeInfo.keySet().stream()
            .filter(key -> key.getFirst().equals(universeUUID))
            .collect(Collectors.toList());
    universeNodeInfos.forEach(uploadedNodeInfo::remove);
  }

  private boolean isShutdown() {
    return universeExecutor.isShutdown() || nodeExecutor.isShutdown();
  }

  private static ExecutorService createUniverseExecutor(
      PlatformExecutorFactory executorFactory, Config runtimeConfig) {
    // TODO: use YBThreadPoolExecutorFactory
    int numParallelism = getThreadpoolParallelism(runtimeConfig);

    // Initialize the health check thread pool.
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("Health-Check-Pool-%d").build();
    // Create an task pool which can handle an unbounded number of tasks, while using an initial
    // set of threads that get spawned up to TASK_THREADS limit.
    return executorFactory.createFixedExecutor(
        "Health-Check-Pool", numParallelism, namedThreadFactory);
  }

  private static ExecutorService createNodeExecutor(
      PlatformExecutorFactory executorFactory, Config runtimeConfig) {
    int numParallelism = runtimeConfig.getInt(HealthChecker.MAX_NUM_THREADS_NODE_CHECK_KEY);

    // Initialize the health check thread pool.
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("Health-Check-Node-Pool-%d").build();

    return executorFactory.createFixedExecutor(
        "Health-Check-Node-Pool", numParallelism, namedThreadFactory);
  }

  public CompletableFuture<Void> runHealthCheck(
      CheckSingleUniverseParams params, Boolean ignoreLastCheck) {
    String universeName = params.universe.getName();
    CompletableFuture<Void> lastCheck =
        this.runningHealthChecks.get(params.universe.getUniverseUUID());
    // Only schedule a task if the previous one for the given universe has completed.
    if (!ignoreLastCheck && lastCheck != null && !lastCheck.isDone()) {
      log.info("Health check for universe {} is still running. Skipping...", universeName);
      return lastCheck;
    }

    log.debug("Scheduling health check for universe: {}", universeName);
    long scheduled = System.currentTimeMillis();
    CompletableFuture<Void> task =
        CompletableFuture.runAsync(
            () -> {
              long diff = System.currentTimeMillis() - scheduled;
              log.debug(
                  "Health check for universe {} was queued for [ {} ms ]", universeName, diff);
              try {
                log.info("Running health check for universe: {}", universeName);
                checkSingleUniverse(params);
              } catch (CancellationException | CompletionException e) {
                log.info(
                    "Health check for universe {} cancelled due to another task started",
                    universeName);
              } catch (Exception e) {
                log.error("Error running health check for universe: {}", universeName, e);
                setHealthCheckFailedMetric(params.customer, params.universe);
              } finally {
                runningHealthChecks.remove(params.universe.getUniverseUUID());
              }
            },
            this.universeExecutor);

    // Add the task to the map of running tasks.
    this.runningHealthChecks.put(params.universe.getUniverseUUID(), task);

    return task;
  }

  private String getAlertDestinations(Universe u, Customer c) {
    List<String> destinations = emailHelper.getDestinations(c.getUuid());
    if (destinations.size() == 0) {
      return null;
    }

    // Earlier we used to use "Universe.DISABLE_ALERTS_UNTIL" to check whether or not to silence
    // health check notifications via the "configureUniverseAlerts" API.
    // But now we unsnoozed all those notifications and instead recommend to use the maintenance
    // window APIs to manage health check notifications along with normal alerts.

    // Check if there is an active maintenance window for this universe which is supposed to
    // suppress health check alerts.
    boolean maintenanceWindowActiveForUniverse =
        maintenanceService.isHealthCheckNotificationSuppressedForUniverse(u);

    return (maintenanceWindowActiveForUniverse) ? null : String.join(",", destinations);
  }

  public void checkSingleUniverse(CheckSingleUniverseParams params) {
    // Validate universe data and make sure nothing is in progress.
    UniverseDefinitionTaskParams details = params.universe.getUniverseDetails();
    if (details == null) {
      log.warn(
          "Skipping universe " + params.universe.getName() + " due to invalid details json...");
      setHealthCheckFailedMetric(params.customer, params.universe);
      return;
    }
    if (details.universePaused) {
      log.warn(
          "Skipping universe " + params.universe.getName() + " as it is in the paused state...");
      return;
    }
    if (details.isUniverseBusyByTask()) {
      log.warn("Skipping universe " + params.universe.getName() + " due to task in progress...");
      return;
    }
    Date startTime = new Date();
    List<NodeInfo> nodeMetadata = new ArrayList<>();
    String providerCode;
    int masterIndex = 0;
    int tserverIndex = 0;
    CustomerTask lastTask = CustomerTask.getLastTaskByTargetUuid(params.universe.getUniverseUUID());
    Long potentialStartTime = null;
    if (lastTask != null && lastTask.getCompletionTime() != null) {
      potentialStartTime = lastTask.getCompletionTime().getTime();
    }
    boolean isUniverseTxnXClusterTarget =
        XClusterConfig.getByTargetUniverseUUID(params.universe.getUniverseUUID()).stream()
            .anyMatch(x -> (x.getType() == ConfigType.Txn));
    boolean testReadWrite =
        confGetter.getConfForScope(params.universe, UniverseConfKeys.dbReadWriteTest)
            && !isUniverseTxnXClusterTarget;
    boolean testYsqlshConnectivity =
        confGetter.getConfForScope(params.universe, UniverseConfKeys.ysqlshConnectivityTest);
    boolean testCqlshConnectivity =
        confGetter.getConfForScope(params.universe, UniverseConfKeys.cqlshConnectivityTest);
    for (UniverseDefinitionTaskParams.Cluster cluster : details.clusters) {
      UserIntent userIntent = cluster.userIntent;
      Provider provider = Provider.get(UUID.fromString(userIntent.provider));
      if (provider == null) {
        log.warn(
            "Skipping universe "
                + params.universe.getName()
                + " due to invalid provider "
                + cluster.userIntent.provider);
        setHealthCheckFailedMetric(params.customer, params.universe);

        return;
      }
      providerCode = provider.getCode();
      List<NodeDetails> activeNodes =
          details.getNodesInCluster(cluster.uuid).stream()
              .filter(NodeDetails::isActive)
              .collect(Collectors.toList());
      for (NodeDetails nd : activeNodes) {
        if (nd.cloudInfo.private_ip == null) {
          log.warn(
              String.format(
                  "Universe %s has active unprovisioned node %s.",
                  params.universe.getName(), nd.nodeName));
          setHealthCheckFailedMetric(params.customer, params.universe);
          return;
        }
      }
      List<NodeDetails> sortedDetails =
          activeNodes.stream()
              .sorted(Comparator.comparing(NodeDetails::getNodeName))
              .collect(Collectors.toList());
      Set<UUID> nodeUuids =
          sortedDetails.stream()
              .map(NodeDetails::getNodeUuid)
              .filter(Objects::nonNull)
              .collect(Collectors.toSet());
      Map<UUID, NodeInstance> nodeInstanceMap =
          NodeInstance.listByUuids(nodeUuids).stream()
              .collect(Collectors.toMap(NodeInstance::getNodeUuid, Function.identity()));
      for (NodeDetails nodeDetails : sortedDetails) {
        NodeInstance nodeInstance = nodeInstanceMap.get(nodeDetails.getNodeUuid());
        String nodeIdentifier = StringUtils.EMPTY;
        if (nodeInstance != null && nodeInstance.getDetails().instanceName != null) {
          nodeIdentifier = nodeInstance.getDetails().instanceName;
        }
        NodeInfo nodeInfo =
            new NodeInfo()
                .setNodeHost(nodeDetails.cloudInfo.private_ip)
                .setNodeName(nodeDetails.nodeName)
                .setNodeUuid(nodeDetails.nodeUuid)
                .setNodeIdentifier(nodeIdentifier)
                .setYbSoftwareVersion(userIntent.ybSoftwareVersion)
                .setEnableYSQL(userIntent.enableYSQL)
                .setEnableYCQL(userIntent.enableYCQL)
                .setEnableYEDIS(userIntent.enableYEDIS)
                .setEnableTls(userIntent.enableNodeToNodeEncrypt)
                .setEnableTlsClient(userIntent.enableClientToNodeEncrypt)
                .setRootAndClientRootCASame(details.rootAndClientRootCASame)
                .setEnableYSQLAuth(userIntent.isYSQLAuthEnabled())
                .setYbHomeDir(provider.getYbHome())
                .setNodeStartTime(potentialStartTime)
                .setTestReadWrite(testReadWrite)
                .setTestYsqlshConnectivity(testYsqlshConnectivity)
                .setTestCqlshConnectivity(testCqlshConnectivity)
                .setUniverseUuid(params.universe.getUniverseUUID())
                .setNodeDetails(nodeDetails);
        if (nodeDetails.isMaster) {
          nodeInfo
              .setMasterIndex(masterIndex++)
              .setMasterHttpPort(nodeDetails.masterHttpPort)
              .setMasterRpcPort(nodeDetails.masterRpcPort);
        }
        if (nodeDetails.isTserver) {
          nodeInfo
              .setTserverIndex(tserverIndex++)
              .setTserverHttpPort(nodeDetails.tserverHttpPort)
              .setTserverRpcPort(nodeDetails.tserverRpcPort);
        }
        if (providerCode.equals(Common.CloudType.kubernetes.toString())) {
          nodeInfo.setK8s(true);
        }
        Map<String, String> tserverGflags =
            GFlagsUtil.getGFlagsForNode(
                nodeDetails, UniverseTaskBase.ServerType.TSERVER, cluster, details.clusters);
        if (tserverGflags.containsKey("ssl_protocols")) {
          nodeInfo.setSslProtocol(tserverGflags.get("ssl_protocols"));
        }
        if (nodeInfo.enableYSQL && nodeDetails.isYsqlServer) {
          nodeInfo.setYsqlPort(nodeDetails.ysqlServerRpcPort);
          nodeInfo.setYsqlServerHttpPort(nodeDetails.ysqlServerHttpPort);
        }
        if (nodeInfo.enableYCQL && nodeDetails.isYqlServer) {
          nodeInfo.setYcqlPort(nodeDetails.yqlServerRpcPort);
        }
        if (nodeInfo.enableYEDIS && nodeDetails.isRedisServer) {
          nodeInfo.setRedisPort(nodeDetails.redisServerRpcPort);
        }
        if (!provider.getCode().equals(CloudType.onprem.toString())
            && !provider.getCode().equals(CloudType.kubernetes.toString())) {
          nodeInfo.setCheckClock(true);
        }
        if (params.universe.isYbcEnabled()) {
          nodeInfo
              .setEnableYbc(true)
              .setYbcPort(
                  params.universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort)
              .setYbcDir(
                  nodeInfo.isK8s()
                      ? String.format(
                          CommonUtils.DEFAULT_YBC_DIR,
                          GFlagsUtil.getCustomTmpDirectory(nodeDetails, params.universe))
                      : nodeInfo.getYbHomeDir());
        }
        nodeMetadata.add(nodeInfo);
      }
    }

    // If last check had errors, set the flag to send an email. If this check will have an error,
    // we would send an email anyway, but if this check shows a healthy universe, let's send an
    // email about it.
    HealthCheck lastCheck = HealthCheck.getLatest(params.universe.getUniverseUUID());
    boolean lastCheckHadErrors = lastCheck != null && lastCheck.hasError();

    // Exit without calling script if the universe is in the "updating" state.
    // Doing the check before the Python script is executed.
    if (!canHealthCheckUniverse(params.universe.getUniverseUUID()) || isShutdown()) {
      return;
    }

    List<NodeData> nodeReports = checkNodes(params.universe, nodeMetadata);

    Details fullReport =
        new Details()
            .setTimestampIso(startTime)
            .setYbVersion(details.getPrimaryCluster().userIntent.ybSoftwareVersion)
            .setData(nodeReports)
            .setHasError(nodeReports.stream().anyMatch(NodeData::getHasError))
            .setHasWarning(nodeReports.stream().anyMatch(NodeData::getHasWarning));
    Details healthCheckReport = removeMetricOnlyChecks(fullReport);

    // Checking the interruption necessity after the Python script finished.
    // It is not needed to analyze results if the universe has the "update in
    // progress" state.
    if (!canHealthCheckUniverse(params.universe.getUniverseUUID()) || isShutdown()) {
      return;
    }

    long durationMs = System.currentTimeMillis() - startTime.getTime();
    boolean sendMailAlways = (params.shouldSendStatusUpdate || lastCheckHadErrors);

    processMetrics(params.customer, params.universe, fullReport);

    log.info(
        "Health check for universe {} reported {}. [ {} ms ]",
        params.universe.getName(),
        (healthCheckReport.getHasError() ? "errors" : "success"),
        durationMs);
    if (healthCheckReport.getHasError()) {
      List<NodeData> failedChecks =
          healthCheckReport.getData().stream()
              .filter(NodeData::getHasError)
              .collect(Collectors.toList());
      log.warn(
          "Following checks failed for universe {}:\n{}",
          params.universe.getName(),
          failedChecks.stream()
              .map(NodeData::toHumanReadableString)
              .collect(Collectors.joining("\n")));
    }

    if (!params.onlyMetrics) {
      if (sendEmailReport(
          params.customer,
          params.universe,
          params.emailDestinations,
          sendMailAlways,
          params.reportOnlyErrors,
          healthCheckReport)) {
        metricService.setOkStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, params.universe));
      }

      HealthCheck.addAndPrune(
          params.universe.getUniverseUUID(), params.universe.getCustomerId(), healthCheckReport);
    }

    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, params.universe));
  }

  private List<NodeData> checkNodes(Universe universe, List<NodeInfo> nodes) {
    // Check if it should log the output of the command.
    boolean shouldLogOutput =
        confGetter.getConfForScope(universe, UniverseConfKeys.healthLogOutput);
    int nodeCheckTimeoutSec =
        confGetter.getConfForScope(universe, UniverseConfKeys.nodeCheckTimeoutSec);

    Map<String, CompletableFuture<Details>> nodeChecks = new HashMap<>();
    for (NodeInfo nodeInfo : nodes) {
      nodeChecks.put(
          nodeInfo.getNodeName(),
          CompletableFuture.supplyAsync(
              () -> checkNode(universe, nodeInfo, shouldLogOutput, nodeCheckTimeoutSec),
              nodeExecutor));
    }

    List<NodeData> result = new ArrayList<>();

    for (NodeInfo nodeInfo : nodes) {
      NodeData nodeStatus =
          new NodeData()
              .setNode(nodeInfo.nodeHost)
              .setNodeName(nodeInfo.nodeName)
              .setNodeIdentifier(nodeInfo.nodeIdentifier)
              .setMessage("Node")
              .setTimestampIso(new Date());
      try {
        CompletableFuture<Details> future = nodeChecks.get(nodeInfo.getNodeName());
        result.addAll(future.get().getData());
        result.add(
            nodeStatus
                .setDetails(Collections.singletonList("Node check succeeded"))
                // We do not want this check to be displayed, unless node check fails.
                .setMetricsOnly(true)
                .setHasError(false));
      } catch (Exception e) {
        // In case error comes from python script - we're getting python wrapper output.
        // Let's remove it for readability
        String message = e.getMessage();
        if (StringUtils.isNotBlank(message) && message.contains(PYTHON_WRAPPER_OUTPUT_PREFIX)) {
          message =
              message
                  .substring(
                      message.lastIndexOf(PYTHON_WRAPPER_OUTPUT_PREFIX)
                          + PYTHON_WRAPPER_OUTPUT_PREFIX.length())
                  .trim();
        }
        log.warn(
            "Error occurred while performing health check for node "
                + nodeInfo.getNodeName()
                + ":"
                + message);
        result.add(
            nodeStatus
                .setDetails(Collections.singletonList("Node check failed: " + message))
                .setHasError(true));
      }
    }

    return result;
  }

  private Details checkNode(
      Universe universe, NodeInfo nodeInfo, boolean logOutput, int timeoutSec) {
    Pair<UUID, String> nodeKey = new Pair<>(universe.getUniverseUUID(), nodeInfo.getNodeName());
    NodeInfo uploadedInfo = uploadedNodeInfo.get(nodeKey);
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(logOutput)
            .traceLogging(true)
            .timeoutSecs(timeoutSec)
            .build();
    if (uploadedInfo == null && !nodeInfo.isK8s()) {
      // Only upload it once for new node, as it only depends on yb home dir.
      // Also skip upload for k8s as no one will call it on k8s pod.
      String generatedScriptPath =
          generateCollectMetricsScript(universe.getUniverseUUID(), nodeInfo);

      String scriptPath = nodeInfo.getYbHomeDir() + "/bin/collect_metrics.sh";
      nodeUniverseManager
          .uploadFileToNode(
              nodeInfo.nodeDetails,
              universe,
              generatedScriptPath,
              scriptPath,
              SCRIPT_PERMISSIONS,
              context)
          .processErrors();
    }

    String scriptPath =
        Paths.get(
                (nodeInfo.isK8s()
                    ? KubernetesTaskBase.K8S_NODE_YW_DATA_DIR
                    : nodeInfo.getYbHomeDir()),
                "/bin/node_health.py")
            .toString();
    if (uploadedInfo == null || !uploadedInfo.equals(nodeInfo)) {
      log.info("Uploading health check script to node {}", nodeInfo.getNodeName());
      String generatedScriptPath = generateNodeCheckScript(universe.getUniverseUUID(), nodeInfo);

      nodeUniverseManager
          .uploadFileToNode(
              nodeInfo.nodeDetails,
              universe,
              generatedScriptPath,
              scriptPath,
              SCRIPT_PERMISSIONS,
              context)
          .processErrors();
    }
    uploadedNodeInfo.put(nodeKey, nodeInfo);

    ShellResponse response =
        nodeUniverseManager
            .runCommand(nodeInfo.getNodeDetails(), universe, scriptPath, context)
            .processErrors();

    return Json.fromJson(Json.parse(response.extractRunCommandOutput()), Details.class);
  }

  private void setHealthCheckFailedMetric(Customer customer, Universe universe) {
    // Remove old metrics and create only health check failed.
    MetricFilter toClean =
        metricSourceKeysFilterWithHealthScriptMetrics(customer, universe, HEALTH_CHECK_METRICS);
    Metric healthCheckFailed =
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, universe).setValue(0.0);
    metricService.cleanAndSave(Collections.singletonList(healthCheckFailed), toClean);
  }

  private String generateCollectMetricsScript(UUID universeUuid, NodeInfo nodeInfo) {
    String template;
    try (InputStream templateStream =
        environment.resourceAsStream("metric/collect_metrics.sh.template")) {
      template = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
      String scriptContent = template.replace("{{YB_HOME_DIR}}", nodeInfo.ybHomeDir);
      // For now it has no universe/cluster specific info. Add placeholder substitution here once
      // they are added.
      Path path =
          fileHelperService.createTempFile(
              "collect_metrics_" + universeUuid + "_" + nodeInfo.nodeUuid, ".sh");
      Files.writeString(path, scriptContent);

      return path.toString();
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition rule template", e);
    }
  }

  private String generateNodeCheckScript(UUID universeUuid, NodeInfo nodeInfo) {
    String template;
    try (InputStream templateStream =
        environment.resourceAsStream("health/node_health.py.template")) {
      template = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
      Universe universe = Universe.getOrBadRequest(universeUuid);
      String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(nodeInfo.nodeDetails, universe);
      String scriptContent = template.replace("{{NODE_INFO}}", Json.toJson(nodeInfo).toString());
      scriptContent = scriptContent.replace("{{TMP_DIR}}", customTmpDirectory);
      // For now it has no universe/cluster specific info. Add placeholder substitution here once
      // they are added.
      Path path =
          fileHelperService.createTempFile(
              "node_health_" + universeUuid + "_" + nodeInfo.nodeUuid, ".py");
      Files.writeString(path, scriptContent);

      return path.toString();
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition rule template", e);
    }
  }

  private MetricFilter metricSourceKeysFilterWithHealthScriptMetrics(
      Customer customer, Universe universe, List<PlatformMetrics> metrics) {
    Set<String> allMetricNames = new HashSet<>(healthScriptMetrics);
    allMetricNames.addAll(
        metrics.stream().map(PlatformMetrics::getMetricName).collect(Collectors.toList()));
    List<MetricSourceKey> metricSourceKeys =
        allMetricNames.stream()
            .map(
                metricName ->
                    MetricSourceKey.builder()
                        .customerUuid(customer.getUuid())
                        .name(metricName)
                        .sourceUuid(universe.getUniverseUUID())
                        .build())
            .collect(Collectors.toList());
    return MetricFilter.builder().sourceKeys(metricSourceKeys).build();
  }

  @VisibleForTesting
  static boolean canHealthCheckUniverse(UUID universeUUID) {
    Optional<Universe> u = Universe.maybeGet(universeUUID);
    UniverseDefinitionTaskParams universeDetails = u.map(Universe::getUniverseDetails).orElse(null);
    if (universeDetails == null) {
      log.warn(
          "Cancelling universe "
              + universeUUID
              + " health-check, the universe not found or empty universe details.");
      return false;
    }

    if (universeDetails.isUniverseBusyByTask()) {
      log.warn(
          "Cancelling universe " + u.get().getName() + " health-check, some task is in progress.");
      return false;
    }
    return true;
  }

  @Data
  @Accessors(chain = true)
  public static class NodeInfo {
    private int masterIndex = -1;
    private int tserverIndex = -1;
    private boolean isK8s = false;
    private String ybHomeDir;
    private String ybcDir = "";
    private String nodeHost;
    private String nodeName;
    private UUID nodeUuid;
    private String nodeIdentifier = "";
    private String ybSoftwareVersion = null;
    private boolean enableTls = false;
    private boolean enableTlsClient = false;
    private boolean rootAndClientRootCASame = true;
    private String sslProtocol = "";
    private boolean enableYSQL = false;
    private boolean enableYCQL = false;
    private boolean enableYSQLAuth = false;
    private int ysqlPort = 5433;
    private int ycqlPort = 9042;
    private boolean enableYEDIS = false;
    private int redisPort = 6379;
    private int masterHttpPort = 7000;
    private int tserverHttpPort = 9000;
    private int masterRpcPort = 7100;
    private int tserverRpcPort = 9100;
    private int ysqlServerHttpPort = 13000;
    private boolean checkClock = false;
    private Long nodeStartTime = null;
    private boolean testReadWrite = true;
    private boolean testYsqlshConnectivity = true;
    private boolean testCqlshConnectivity = true;
    private boolean enableYbc = false;
    private int ybcPort = 18018;
    private UUID universeUuid;
    @JsonIgnore @EqualsAndHashCode.Exclude private NodeDetails nodeDetails;
  }

  private Details removeMetricOnlyChecks(Details details) {
    List<NodeData> nodeReports =
        details.getData().stream()
            .filter(data -> !data.getMetricsOnly())
            .collect(Collectors.toList());
    return new Details()
        .setTimestampIso(details.getTimestampIso())
        .setYbVersion(details.getYbVersion())
        .setData(nodeReports)
        .setHasError(nodeReports.stream().anyMatch(NodeData::getHasError))
        .setHasWarning(nodeReports.stream().anyMatch(NodeData::getHasWarning));
  }
}
