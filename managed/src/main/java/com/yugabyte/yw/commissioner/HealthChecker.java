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

import akka.Done;
import akka.actor.ActorSystem;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.HealthManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Gauge;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import javax.mail.MessagingException;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.inject.ApplicationLifecycle;
import play.libs.Json;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
public class HealthChecker {
  public static final Logger LOG = LoggerFactory.getLogger(HealthChecker.class);

  public static final String kUnivMetricName = "yb_univ_health_status";
  public static final String kUnivUUIDLabel = "univ_uuid";
  public static final String kUnivNameLabel = "univ_name";
  public static final String kCheckLabel = "check_name";
  public static final String kNodeLabel = "node";

  private static final List<PlatformMetrics> HEALTH_CHECK_METRICS =
      ImmutableList.<PlatformMetrics>builder()
          .add(PlatformMetrics.HEALTH_CHECK_STATUS)
          .add(PlatformMetrics.HEALTH_CHECK_MASTER_DOWN)
          .add(PlatformMetrics.HEALTH_CHECK_MASTER_FATAL_LOGS)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_DOWN)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_FATAL_LOGS)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_CORE_FILES)
          .add(PlatformMetrics.HEALTH_CHECK_YSQLSH_CONNECTIVITY_ERROR)
          .add(PlatformMetrics.HEALTH_CHECK_CQLSH_CONNECTIVITY_ERROR)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_DISK_UTILIZATION_HIGH)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_OPENED_FD_HIGH)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_CLOCK_SYNCHRONIZATION_ERROR)
          .build();

  private static final String YB_TSERVER_PROCESS = "yb-tserver";

  private static final String UPTIME_CHECK = "Uptime";
  private static final String FATAL_LOG_CHECK = "Fatal log files";
  private static final String CQLSH_CONNECTIVITY_CHECK = "Connectivity with cqlsh";
  private static final String YSQLSH_CONNECTIVITY_CHECK = "Connectivity with ysqlsh";
  private static final String DISK_UTILIZATION_CHECK = "Disk utilization";
  private static final String CORE_FILES_CHECK = "Core files";
  private static final String OPENED_FILE_DESCRIPTORS_CHECK = "Opened file descriptors";
  private static final String CLOCK_SYNC_CHECK = "Clock synchronization";

  private static final String MAX_NUM_THREADS_KEY = "yb.health.max_num_parallel_checks";

  private final play.Configuration config;

  // Last time we sent a status update email per customer.
  private final Map<UUID, Long> lastStatusUpdateTimeMap = new HashMap<>();

  // Last time we actually ran the health check script per customer.
  private final Map<UUID, Long> lastCheckTimeMap = new HashMap<>();

  // What will run the health checking script.
  HealthManager healthManager;

  private Gauge healthMetric = null;

  private final AtomicBoolean running = new AtomicBoolean(false);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final CollectorRegistry promRegistry;

  private final HealthCheckerReport healthCheckerReport;

  private final EmailHelper emailHelper;

  private final MetricService metricService;

  private final RuntimeConfigFactory runtimeConfigFactory;

  // The thread pool executor for parallelized health checks.
  private final ExecutorService executor;

  // A map of all running health checks.
  final Map<UUID, CompletableFuture<Void>> runningHealthChecks = new ConcurrentHashMap<>();

  final ApplicationLifecycle lifecycle;

  @VisibleForTesting
  HealthChecker(
      ActorSystem actorSystem,
      Configuration config,
      ExecutionContext executionContext,
      HealthManager healthManager,
      CollectorRegistry promRegistry,
      HealthCheckerReport healthCheckerReport,
      EmailHelper emailHelper,
      MetricService metricService,
      RuntimeConfigFactory runtimeConfigFactory,
      ApplicationLifecycle lifecycle) {
    this.actorSystem = actorSystem;
    this.config = config;
    this.executionContext = executionContext;
    this.healthManager = healthManager;
    this.promRegistry = promRegistry;
    this.healthCheckerReport = healthCheckerReport;
    this.emailHelper = emailHelper;
    this.metricService = metricService;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.lifecycle = lifecycle;
    this.executor = this.createExecutor();

    this.initialize();
  }

  @Inject
  public HealthChecker(
      ActorSystem globalActorSystem,
      Configuration config,
      ExecutionContext executionContext,
      HealthManager healthManager,
      HealthCheckerReport healthCheckerReport,
      EmailHelper emailHelper,
      MetricService metricService,
      RuntimeConfigFactory runtimeConfigFactory,
      ApplicationLifecycle lifecycle) {
    this(
        globalActorSystem,
        config,
        executionContext,
        healthManager,
        CollectorRegistry.defaultRegistry,
        healthCheckerReport,
        emailHelper,
        metricService,
        runtimeConfigFactory,
        lifecycle);
  }

  private void initialize() {
    LOG.info("Scheduling health checker every " + this.healthCheckIntervalMs() + " ms");
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.create(0, TimeUnit.MILLISECONDS), // initialDelay
            Duration.create(this.healthCheckIntervalMs(), TimeUnit.MILLISECONDS), // interval
            this::scheduleRunner,
            this.executionContext);

    try {
      healthMetric =
          Gauge.build(kUnivMetricName, "Boolean result of health checks")
              .labelNames(kUnivUUIDLabel, kUnivNameLabel, kNodeLabel, kCheckLabel)
              .register(this.promRegistry);
    } catch (IllegalArgumentException e) {
      LOG.warn("Failed to build prometheus gauge for name: " + kUnivMetricName);
    }

    // Add shutdown hook to kill the task pool
    if (this.lifecycle != null) {
      this.lifecycle.addStopHook(this::shutdownThreadpool);
    }
  }

  // The interval at which the checker will run.
  // Can be overridden per customer.
  private long healthCheckIntervalMs() {
    Long interval = config.getLong("yb.health.check_interval_ms");
    return interval == null ? 0 : interval;
  }

  // The interval at which to send a status update of all the current universes.
  // Can be overridden per customer.
  private long statusUpdateIntervalMs() {
    Long interval = config.getLong("yb.health.status_interval_ms");
    return interval == null ? 0 : interval;
  }

  private boolean processResults(
      Customer c,
      Universe u,
      String response,
      long durationMs,
      String emailDestinations,
      boolean sendMailAlways,
      boolean reportOnlyErrors) {

    JsonNode healthJSON;
    try {
      healthJSON = Util.convertStringToJson(response);
    } catch (Exception e) {
      LOG.warn("Failed to convert health check response to JSON " + e.getMessage());
      setHealthCheckFailedMetric(
          c, u, "Error converting health check response to JSON: " + e.getMessage());
      return false;
    }

    if (healthJSON != null) {
      boolean hasErrors = false;
      // This is hacky, but health check data items only make sense if you know order.
      boolean isMaster = true;
      try {
        Map<PlatformMetrics, Integer> platformMetrics = new HashMap<>();
        Set<String> nodesWithError = new HashSet<>();
        for (JsonNode entry : healthJSON.path("data")) {
          String nodeName = entry.path("node").asText();
          String checkName = entry.path("message").asText();
          JsonNode process = entry.path("process");
          boolean checkResult = entry.path("has_error").asBoolean();
          if (checkResult) {
            nodesWithError.add(nodeName);
          }
          hasErrors = checkResult || hasErrors;

          if (!process.isMissingNode() && process.asText().equals(YB_TSERVER_PROCESS)) {
            isMaster = false;
          }
          PlatformMetrics metric = getMetricByCheckName(checkName, isMaster);
          if (metric != null) {
            // checkResult == true -> error -> 1
            int toAppend = checkResult ? 1 : 0;
            platformMetrics.compute(metric, (k, v) -> v != null ? v + toAppend : toAppend);
          }
          if (null == healthMetric) continue;

          Gauge.Child prometheusVal =
              healthMetric.labels(u.universeUUID.toString(), u.name, nodeName, checkName);
          prometheusVal.set(checkResult ? 1 : 0);
        }
        LOG.info(
            "Health check for universe {} reported {}. [ {} ms ]",
            u.name,
            (hasErrors ? "errors" : " success"),
            durationMs);

        List<Metric> metrics =
            platformMetrics
                .entrySet()
                .stream()
                .map(
                    e ->
                        metricService
                            .buildMetricTemplate(e.getKey(), u)
                            .setValue(e.getValue().doubleValue()))
                .collect(Collectors.toList());
        metricService.cleanAndSave(metrics);

        metricService.setMetric(
            metricService.buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODES_WITH_ERRORS, u),
            nodesWithError.size());

        metricService.setOkStatusMetric(
            metricService.buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODE_METRICS_STATUS, u));
      } catch (Exception e) {
        LOG.warn("Failed to convert health check response to prometheus metrics " + e.getMessage());
        metricService.setStatusMetric(
            metricService.buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODE_METRICS_STATUS, u),
            "Error converting health check response to prometheus metrics: " + e.getMessage());
      }

      SmtpData smtpData = emailHelper.getSmtpData(c.uuid);
      boolean mailSendError = false;
      if (!StringUtils.isEmpty(emailDestinations)
          && (smtpData != null)
          && (sendMailAlways || hasErrors)) {
        String subject =
            String.format("%s - <%s> %s", hasErrors ? "ERROR" : "OK", c.getTag(), u.name);
        String mailError =
            sendEmailReport(
                u, c, smtpData, emailDestinations, subject, healthJSON, reportOnlyErrors);
        if (mailError != null) {
          LOG.warn("Health check had the following errors during mailing: " + mailError);
          metricService.setStatusMetric(
              metricService.buildMetricTemplate(
                  PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, u),
              "Error sending Health check email: " + mailError);
          mailSendError = true;
        }
      }
      if (!mailSendError) {
        metricService.setOkStatusMetric(
            metricService.buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, u));
      }
    }
    return true;
  }

  private String sendEmailReport(
      Universe u,
      Customer c,
      SmtpData smtpData,
      String emailDestinations,
      String subject,
      JsonNode report,
      boolean reportOnlyErrors) {

    // LinkedHashMap saves values order.
    Map<String, String> contentMap = new LinkedHashMap<>();
    contentMap.put(
        "text/plain; charset=\"us-ascii\"",
        healthCheckerReport.asPlainText(report, reportOnlyErrors));
    contentMap.put(
        "text/html; charset=\"us-ascii\"", healthCheckerReport.asHtml(u, report, reportOnlyErrors));

    try {
      emailHelper.sendEmail(c, subject, emailDestinations, smtpData, contentMap);
    } catch (MessagingException e) {
      return e.getMessage();
    }

    return null;
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (HighAvailabilityConfig.isFollower()) {
      LOG.debug("Skipping health check scheduler for follower platform");
      return;
    }

    if (running.get()) {
      LOG.info("Previous run of health check scheduler is still underway");
      return;
    }

    running.set(true);
    // TODO(bogdan): This will not be too DB friendly when we go multi-tenant.
    for (Customer c : Customer.getAll()) {
      try {
        checkCustomer(c);
      } catch (Exception ex) {
        LOG.error("Error running health check scheduler for customer " + c.uuid, ex);
      }
    }

    running.set(false);
  }

  public void checkCustomer(Customer c) {
    // We need an alerting config to do work.
    CustomerConfig config = CustomerConfig.getAlertConfig(c.uuid);
    if (config == null) {
      LOG.info("Skipping customer " + c.uuid + " due to missing alerting config...");
      return;
    }

    AlertingData alertingData = Json.fromJson(config.data, AlertingData.class);
    long now = (new Date()).getTime();
    long checkIntervalMs =
        alertingData.checkIntervalMs <= 0 ? healthCheckIntervalMs() : alertingData.checkIntervalMs;
    boolean shouldRunCheck = (now - checkIntervalMs) > lastCheckTimeMap.getOrDefault(c.uuid, 0L);
    long statusUpdateIntervalMs =
        alertingData.statusUpdateIntervalMs <= 0
            ? statusUpdateIntervalMs()
            : alertingData.statusUpdateIntervalMs;
    boolean shouldSendStatusUpdate =
        (now - statusUpdateIntervalMs) > lastStatusUpdateTimeMap.getOrDefault(c.uuid, 0L);
    // Always do a check if it's time for a status update OR if it's time for a check.
    if (shouldSendStatusUpdate || shouldRunCheck) {
      // Since we'll do a check, update this all the time.
      lastCheckTimeMap.put(c.uuid, now);
      if (shouldSendStatusUpdate) {
        lastStatusUpdateTimeMap.put(c.uuid, now);
      }
      checkAllUniverses(c, config, shouldSendStatusUpdate);
    }
  }

  static class CheckSingleUniverseParams {
    final Universe universe;
    final Customer customer;
    final boolean shouldSendStatusUpdate;
    final boolean reportOnlyErrors;
    final String emailDestinations;

    public CheckSingleUniverseParams(
        Universe universe,
        Customer customer,
        boolean shouldSendStatusUpdate,
        boolean reportOnlyErrors,
        String emailDestinations) {
      this.universe = universe;
      this.customer = customer;
      this.shouldSendStatusUpdate = shouldSendStatusUpdate;
      this.reportOnlyErrors = reportOnlyErrors;
      this.emailDestinations = emailDestinations;
    }
  }

  @VisibleForTesting
  Config getRuntimeConfig() {
    return this.runtimeConfigFactory.globalRuntimeConf();
  }

  private int getThreadpoolParallelism() {
    return this.getRuntimeConfig().getInt(HealthChecker.MAX_NUM_THREADS_KEY);
  }

  public void checkAllUniverses(Customer c, CustomerConfig config, boolean shouldSendStatusUpdate) {

    AlertingData alertingData =
        config != null ? Json.fromJson(config.data, AlertingData.class) : null;
    boolean reportOnlyErrors =
        !shouldSendStatusUpdate && alertingData != null && alertingData.reportOnlyErrors;

    c.getUniverses()
        .stream()
        .map(
            u -> {
              String destinations = getAlertDestinations(u, c);
              return new CheckSingleUniverseParams(
                  u, c, shouldSendStatusUpdate, reportOnlyErrors, destinations);
            })
        .forEach(this::runHealthCheck);
  }

  public void cancelHealthCheck(UUID universeUUID) {
    CompletableFuture<Void> lastCheckForUUID = this.runningHealthChecks.get(universeUUID);
    if (lastCheckForUUID == null) {
      return;
    }

    lastCheckForUUID.cancel(true);
  }

  private CompletableFuture<Done> shutdownThreadpool() {
    if (this.executor != null) {
      LOG.info("Shutting down Health Check thread pool");
      this.executor.shutdownNow();
    }

    return CompletableFuture.completedFuture(Done.done());
  }

  private ExecutorService createExecutor() {
    if (this.executor != null) {
      return this.executor;
    }

    int numParallelism = this.getThreadpoolParallelism();

    // Initialize the health check thread pool.
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("Health-Check-Pool-%d").build();
    // Create an task pool which can handle an unbounded number of tasks, while using an initial
    // set of threads that get spawned up to TASK_THREADS limit.
    ExecutorService newExecutor = Executors.newFixedThreadPool(numParallelism, namedThreadFactory);

    LOG.info("Created Health Check thread pool");

    return newExecutor;
  }

  public CompletableFuture<Void> runHealthCheck(CheckSingleUniverseParams params) {
    String universeName = params.universe.name;
    CompletableFuture<Void> lastCheck = this.runningHealthChecks.get(params.universe.universeUUID);
    // Only schedule a task if the previous one for the given universe has completed.
    if (lastCheck != null && !lastCheck.isDone()) {
      LOG.info("Health check for universe {} is still running. Skipping...", universeName);

      return lastCheck;
    }

    LOG.debug("Scheduling health check for universe: {}", universeName);
    long scheduled = System.currentTimeMillis();
    CompletableFuture<Void> task =
        CompletableFuture.runAsync(
            () -> {
              long diff = System.currentTimeMillis() - scheduled;
              LOG.debug(
                  "Health check for universe {} was queued for [ {} ms ]", universeName, diff);
              try {
                LOG.info("Running health check for universe: {}", universeName);
                checkSingleUniverse(params);
              } catch (Exception e) {
                LOG.error("Error running health check for universe: {}", universeName, e);
                setHealthCheckFailedMetric(
                    params.customer,
                    params.universe,
                    "Error running health check: " + e.getMessage());
              }
            },
            this.executor);

    // Add the task to the map of running tasks.
    this.runningHealthChecks.put(params.universe.universeUUID, task);

    return task;
  }

  private String getAlertDestinations(Universe u, Customer c) {
    List<String> destinations = emailHelper.getDestinations(c.uuid);
    if (destinations.size() == 0) {
      return null;
    }

    String disabledUntilStr = u.getConfig().getOrDefault(Universe.DISABLE_ALERTS_UNTIL, "0");
    long disabledUntilSecs = 0;
    try {
      disabledUntilSecs = Long.parseLong(disabledUntilStr);
    } catch (NumberFormatException ne) {
      LOG.warn("invalid universe config for disabled alerts: [ " + disabledUntilStr + " ]");
    }

    boolean silenceEmails = ((System.currentTimeMillis() / 1000) <= disabledUntilSecs);
    return silenceEmails ? null : String.join(",", destinations);
  }

  public void checkSingleUniverse(CheckSingleUniverseParams params) {
    // Validate universe data and make sure nothing is in progress.
    UniverseDefinitionTaskParams details = params.universe.getUniverseDetails();
    if (details == null) {
      LOG.warn("Skipping universe " + params.universe.name + " due to invalid details json...");
      setHealthCheckFailedMetric(
          params.customer, params.universe, "Health check skipped due to invalid details json.");
      return;
    }
    if (details.universePaused) {
      LOG.warn("Skipping universe " + params.universe.name + " as it is in the paused state...");
      return;
    }
    if (details.updateInProgress) {
      LOG.warn("Skipping universe " + params.universe.name + " due to task in progress...");
      return;
    }
    long startMs = System.currentTimeMillis();

    Map<UUID, HealthManager.ClusterInfo> clusterMetadata = new HashMap<>();
    boolean invalidUniverseData = false;
    String providerCode;
    for (UniverseDefinitionTaskParams.Cluster cluster : details.clusters) {
      HealthManager.ClusterInfo info = new HealthManager.ClusterInfo();
      clusterMetadata.put(cluster.uuid, info);
      info.ybSoftwareVersion = cluster.userIntent.ybSoftwareVersion;
      info.enableYSQL = cluster.userIntent.enableYSQL;
      info.enableYEDIS = cluster.userIntent.enableYEDIS;
      if (cluster.userIntent.tserverGFlags.containsKey("ssl_protocols")) {
        info.sslProtocol = cluster.userIntent.tserverGFlags.get("ssl_protocols");
      }
      // Since health checker only uses CQLSH, we only care about the
      // client to node encryption flag.
      info.enableTlsClient = cluster.userIntent.enableClientToNodeEncrypt;
      // Setting this flag to identify correct cert location.
      info.rootAndClientRootCASame = details.rootAndClientRootCASame;
      // Pass in whether YSQL authentication is enabled for the given cluster.
      info.enableYSQLAuth =
          cluster.userIntent.tserverGFlags.getOrDefault("ysql_enable_auth", "false").equals("true");

      Provider provider = Provider.get(UUID.fromString(cluster.userIntent.provider));
      if (provider == null) {
        LOG.warn(
            "Skipping universe "
                + params.universe.name
                + " due to invalid provider "
                + cluster.userIntent.provider);
        invalidUniverseData = true;
        setHealthCheckFailedMetric(
            params.customer, params.universe, "Health check skipped due to invalid provider data.");

        break;
      }

      providerCode = provider.code;
      if (providerCode.equals(Common.CloudType.kubernetes.toString())) {
        info.namespaceToConfig =
            PlacementInfoUtil.getConfigPerNamespace(
                cluster.placementInfo, details.nodePrefix, provider);
      }

      AccessKey accessKey = AccessKey.get(provider.uuid, cluster.userIntent.accessKeyCode);
      if (accessKey == null || accessKey.getKeyInfo() == null) {
        if (!providerCode.equals(CloudType.kubernetes.toString())) {
          LOG.warn("Skipping universe " + params.universe.name + " due to invalid access key...");
          invalidUniverseData = true;
          setHealthCheckFailedMetric(
              params.customer, params.universe, "Health check skipped due to invalid access key.");

          break;
        }
      } else {
        info.identityFile = accessKey.getKeyInfo().privateKey;
        info.sshPort = accessKey.getKeyInfo().sshPort;
      }

      if (info.enableYSQL) {
        for (NodeDetails nd : details.nodeDetailsSet) {
          if (nd.isYsqlServer) {
            info.ysqlPort = nd.ysqlServerRpcPort;
            break;
          }
        }
      }

      for (NodeDetails nd : details.nodeDetailsSet) {
        info.ycqlPort = nd.yqlServerRpcPort;
        break;
      }

      if (info.enableYEDIS) {
        for (NodeDetails nd : details.nodeDetailsSet) {
          if (nd.isRedisServer) {
            info.redisPort = nd.redisServerRpcPort;
            break;
          }
        }
      }
    }

    // If any clusters were invalid, abort for this universe.
    if (invalidUniverseData) {
      return;
    }

    for (NodeDetails nd : details.nodeDetailsSet) {
      if (nd.cloudInfo.private_ip == null) {
        invalidUniverseData = true;
        LOG.warn(
            String.format(
                "Universe %s has unprovisioned node %s.", params.universe.name, nd.nodeName));
        setHealthCheckFailedMetric(
            params.customer,
            params.universe,
            String.format(
                "Can't run health check for the universe due to missing IP address for node %s.",
                nd.nodeName));
        break;
      }

      HealthManager.ClusterInfo info = clusterMetadata.get(nd.placementUuid);
      if (info == null) {
        invalidUniverseData = true;
        LOG.warn(
            String.format(
                "Universe %s has node %s with invalid placement %s",
                params.universe.name, nd.nodeName, nd.placementUuid));
        String alertText =
            String.format(
                "Universe has node %s with invalid placement %s.", nd.nodeName, nd.placementUuid);
        setHealthCheckFailedMetric(params.customer, params.universe, alertText);

        break;
      }

      // TODO: we do not have a good way of marking the whole universe as k8s only.
      if (nd.isMaster) {
        info.masterNodes.put(nd.cloudInfo.private_ip, nd.nodeName);
      }

      if (nd.isTserver) {
        info.tserverNodes.put(nd.cloudInfo.private_ip, nd.nodeName);
      }
    }
    // If any nodes were invalid, abort for this universe.
    if (invalidUniverseData) {
      return;
    }

    CustomerTask lastTask = CustomerTask.getLatestByUniverseUuid(params.universe.universeUUID);
    long potentialStartTime = 0;
    if (lastTask != null && lastTask.getCompletionTime() != null) {
      potentialStartTime = lastTask.getCompletionTime().getTime();
    }

    // If last check had errors, set the flag to send an email. If this check will have an error,
    // we would send an email anyway, but if this check shows a healthy universe, let's send an
    // email about it.
    HealthCheck lastCheck = HealthCheck.getLatest(params.universe.universeUUID);
    boolean lastCheckHadErrors = lastCheck != null && lastCheck.hasError();
    Provider mainProvider =
        Provider.get(UUID.fromString(details.getPrimaryCluster().userIntent.provider));

    // Check if it should log the output of the command.
    Boolean shouldLogOutput = false; // Default value.
    if (runtimeConfigFactory.forUniverse(params.universe).hasPath("yb.health.logOutput")) {
      shouldLogOutput =
          runtimeConfigFactory.forUniverse(params.universe).getBoolean("yb.health.logOutput");
    }

    // Call devops and process response.
    ShellResponse response =
        healthManager.runCommand(
            mainProvider,
            new ArrayList<>(clusterMetadata.values()),
            potentialStartTime,
            shouldLogOutput);

    long durationMs = System.currentTimeMillis() - startMs;
    boolean sendMailAlways = (params.shouldSendStatusUpdate || lastCheckHadErrors);

    if (response.code == 0) {
      boolean succeeded =
          processResults(
              params.customer,
              params.universe,
              response.message,
              durationMs,
              params.emailDestinations,
              sendMailAlways,
              params.reportOnlyErrors);
      HealthCheck.addAndPrune(
          params.universe.universeUUID, params.universe.customerId, response.message);
      if (succeeded) {
        metricService.setOkStatusMetric(
            metricService.buildMetricTemplate(
                PlatformMetrics.HEALTH_CHECK_STATUS, params.universe));
      }
    } else {
      LOG.error(
          "Health check script got error: {} code ({}) [ {} ms ]",
          response.message,
          response.code,
          durationMs);
      String alertText =
          String.format(
              "Health check script got error: %s code (%d) [ %d ms ]",
              response.message, response.code, durationMs);
      setHealthCheckFailedMetric(params.customer, params.universe, alertText);
    }
  }

  private void setHealthCheckFailedMetric(Customer customer, Universe universe, String message) {
    // Remove old metrics and create only health check failed
    List<MetricKey> toClean =
        HEALTH_CHECK_METRICS
            .stream()
            .map(
                nodeMetric ->
                    MetricKey.builder()
                        .customerUuid(customer.getUuid())
                        .name(nodeMetric.getMetricName())
                        .targetUuid(universe.getUniverseUUID())
                        .build())
            .collect(Collectors.toList());
    Metric healthCheckFailed =
        metricService
            .buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, universe)
            .setLabel(KnownAlertLabels.ERROR_MESSAGE, message)
            .setValue(0.0);
    metricService.cleanAndSave(
        Collections.singletonList(healthCheckFailed), MetricFilter.builder().keys(toClean).build());
  }

  private PlatformMetrics getMetricByCheckName(String checkName, boolean isMaster) {
    switch (checkName) {
      case UPTIME_CHECK:
        if (isMaster) {
          return PlatformMetrics.HEALTH_CHECK_MASTER_DOWN;
        } else {
          return PlatformMetrics.HEALTH_CHECK_TSERVER_DOWN;
        }
      case FATAL_LOG_CHECK:
        if (isMaster) {
          return PlatformMetrics.HEALTH_CHECK_MASTER_FATAL_LOGS;
        } else {
          return PlatformMetrics.HEALTH_CHECK_TSERVER_FATAL_LOGS;
        }
      case CQLSH_CONNECTIVITY_CHECK:
        return PlatformMetrics.HEALTH_CHECK_CQLSH_CONNECTIVITY_ERROR;
      case YSQLSH_CONNECTIVITY_CHECK:
        return PlatformMetrics.HEALTH_CHECK_YSQLSH_CONNECTIVITY_ERROR;
      case DISK_UTILIZATION_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_DISK_UTILIZATION_HIGH;
      case CORE_FILES_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_CORE_FILES;
      case OPENED_FILE_DESCRIPTORS_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_OPENED_FD_HIGH;
      case CLOCK_SYNC_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_CLOCK_SYNCHRONIZATION_ERROR;
      default:
        return null;
    }
  }
}
