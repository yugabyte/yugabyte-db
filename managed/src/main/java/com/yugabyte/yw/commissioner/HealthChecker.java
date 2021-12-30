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

import static com.yugabyte.yw.commissioner.HealthCheckMetrics.HEALTH_CHECK_METRICS;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.HEALTH_CHECK_METRICS_WITHOUT_STATUS;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.UPTIME_CHECK;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.getCountMetricByCheckName;
import static com.yugabyte.yw.commissioner.HealthCheckMetrics.getNodeMetrics;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import akka.Done;
import akka.actor.ActorSystem;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
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
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricSourceKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import io.prometheus.client.Gauge;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import javax.mail.MessagingException;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import play.Configuration;
import play.Environment;
import play.inject.ApplicationLifecycle;
import play.libs.Json;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
@Slf4j
public class HealthChecker {

  private static final String YB_TSERVER_PROCESS = "yb-tserver";

  private static final String MAX_NUM_THREADS_KEY = "yb.health.max_num_parallel_checks";

  private final Environment environment;

  private final play.Configuration config;

  // Last time we sent a status update email per customer.
  private final Map<UUID, Long> lastStatusUpdateTimeMap = new HashMap<>();

  // Last time we actually ran the health check script per customer.
  private final Map<UUID, Long> lastCheckTimeMap = new HashMap<>();

  // What will run the health checking script.
  HealthManager healthManager;

  private final AtomicBoolean running = new AtomicBoolean(false);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final HealthCheckerReport healthCheckerReport;

  private final EmailHelper emailHelper;

  private final MetricService metricService;

  private final RuntimeConfigFactory runtimeConfigFactory;

  // The thread pool executor for parallelized health checks.
  private final ExecutorService executor;

  // A map of all running health checks.
  final Map<UUID, CompletableFuture<Void>> runningHealthChecks = new ConcurrentHashMap<>();

  final ApplicationLifecycle lifecycle;

  private final HealthCheckMetrics healthMetrics;

  @Inject
  public HealthChecker(
      Environment environment,
      ActorSystem actorSystem,
      Configuration config,
      ExecutionContext executionContext,
      HealthManager healthManager,
      HealthCheckerReport healthCheckerReport,
      EmailHelper emailHelper,
      MetricService metricService,
      RuntimeConfigFactory runtimeConfigFactory,
      ApplicationLifecycle lifecycle,
      HealthCheckMetrics healthMetrics) {
    this(
        environment,
        actorSystem,
        config,
        executionContext,
        healthManager,
        healthCheckerReport,
        emailHelper,
        metricService,
        runtimeConfigFactory,
        lifecycle,
        healthMetrics,
        createExecutor(runtimeConfigFactory.globalRuntimeConf()));
  }

  HealthChecker(
      Environment environment,
      ActorSystem actorSystem,
      Configuration config,
      ExecutionContext executionContext,
      HealthManager healthManager,
      HealthCheckerReport healthCheckerReport,
      EmailHelper emailHelper,
      MetricService metricService,
      RuntimeConfigFactory runtimeConfigFactory,
      ApplicationLifecycle lifecycle,
      HealthCheckMetrics healthMetrics,
      ExecutorService executorService) {
    this.environment = environment;
    this.actorSystem = actorSystem;
    this.config = config;
    this.executionContext = executionContext;
    this.healthManager = healthManager;
    this.healthCheckerReport = healthCheckerReport;
    this.emailHelper = emailHelper;
    this.metricService = metricService;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.lifecycle = lifecycle;
    this.healthMetrics = healthMetrics;
    this.executor = executorService;

    this.initialize();
  }

  private void initialize() {
    log.info("Scheduling health checker every " + this.healthCheckIntervalMs() + " ms");
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.create(0, TimeUnit.MILLISECONDS), // initialDelay
            Duration.create(this.healthCheckIntervalMs(), TimeUnit.MILLISECONDS), // interval
            this::scheduleRunner,
            this.executionContext);

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
   * Process results received from the health-check script.
   *
   * @param c Customer.
   * @param u Universe.
   * @param response Response data.
   * @param durationMs Duration of the completed health-check.
   * @param emailDestinations Recipients for the email report.
   * @param sendMailAlways Force the email sending.
   * @param reportOnlyErrors Include only errors into the report.
   * @param onlyMetrics Don't send email, only metrics collection.
   * @return
   */
  private boolean processResults(
      Customer c,
      Universe u,
      String response,
      long durationMs,
      String emailDestinations,
      boolean sendMailAlways,
      boolean reportOnlyErrors,
      boolean onlyMetrics) {

    JsonNode healthJSON;
    try {
      healthJSON = Util.convertStringToJson(response);
    } catch (Exception e) {
      log.warn("Failed to convert health check response to JSON " + e.getMessage());
      setHealthCheckFailedMetric(
          c, u, "Error converting health check response to JSON: " + e.getMessage());
      return false;
    }

    if (healthJSON != null) {
      boolean hasErrors = false;
      // This is hacky, but health check data items only make sense if you know order.
      boolean isMaster = true;
      boolean isInstanceUp = false;
      try {
        List<Metric> metrics = new ArrayList<>();
        Map<PlatformMetrics, Integer> platformMetrics = new HashMap<>();
        Set<String> nodesWithError = new HashSet<>();
        for (JsonNode entry : healthJSON.path("data")) {
          String nodeName = entry.path("node").asText();
          String checkName = entry.path("message").asText();
          JsonNode process = entry.path("process");
          JsonNode metricValueNode = entry.path("metric_value");
          Double merticValue = null;
          boolean checkResult = entry.path("has_error").asBoolean();
          if (checkResult) {
            nodesWithError.add(nodeName);
          }
          hasErrors = checkResult || hasErrors;

          // First data node for master and tserver have process field with process type.
          if (!process.isMissingNode()) {
            isMaster = !process.asText().equals(YB_TSERVER_PROCESS);
          }
          // Some data nodes have metric_value fields, which we need to write as a platform metric.
          if (!metricValueNode.isMissingNode()) {
            merticValue = metricValueNode.asDouble();
          }
          // Add node metric value if it's present in data node
          if (merticValue != null) {
            metrics.addAll(getNodeMetrics(checkName, isMaster, u, nodeName, merticValue));
          }
          if (checkName.equals(UPTIME_CHECK)) {
            // No boot time metric means the instance or the whole node is down
            // and this node shouldn't be counted in other error node count metrics.
            isInstanceUp = merticValue != null;
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
          if (null == healthMetrics.getHealthMetric()) continue;

          Gauge.Child prometheusVal =
              healthMetrics
                  .getHealthMetric()
                  .labels(u.universeUUID.toString(), u.name, nodeName, checkName);
          prometheusVal.set(checkResult ? 1 : 0);
        }
        log.info(
            "Health check for universe {} reported {}. [ {} ms ]",
            u.name,
            (hasErrors ? "errors" : " success"),
            durationMs);

        metrics.addAll(
            platformMetrics
                .entrySet()
                .stream()
                .map(e -> buildMetricTemplate(e.getKey(), u).setValue(e.getValue().doubleValue()))
                .collect(Collectors.toList()));
        // Clean all health check metrics for universe before saving current values
        // just in case list of nodes changed between runs.
        MetricFilter toClean = metricSourceKeysFilter(c, u, HEALTH_CHECK_METRICS_WITHOUT_STATUS);
        metricService.cleanAndSave(metrics, toClean);

        metricService.setMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODES_WITH_ERRORS, u),
            nodesWithError.size());

        metricService.setOkStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODE_METRICS_STATUS, u));
      } catch (Exception e) {
        log.warn("Failed to convert health check response to prometheus metrics", e);
        metricService.setStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NODE_METRICS_STATUS, u),
            "Error converting health check response to prometheus metrics: " + e.getMessage());
      }

      if (!onlyMetrics
          && sendEmailReport(
              c, u, emailDestinations, sendMailAlways, hasErrors, reportOnlyErrors, healthJSON)) {
        metricService.setOkStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, u));
      }
    }
    return true;
  }

  private boolean sendEmailReport(
      Customer c,
      Universe u,
      String emailDestinations,
      boolean sendMailAlways,
      boolean hasErrors,
      boolean reportOnlyErrors,
      JsonNode report) {
    SmtpData smtpData = emailHelper.getSmtpData(c.uuid);
    if (!StringUtils.isEmpty(emailDestinations)
        && (smtpData != null)
        && (sendMailAlways || hasErrors)) {
      String subject =
          String.format("%s - <%s> %s", hasErrors ? "ERROR" : "OK", c.getTag(), u.name);

      // LinkedHashMap saves values order.
      Map<String, String> contentMap = new LinkedHashMap<>();
      contentMap.put(
          "text/plain; charset=\"us-ascii\"",
          healthCheckerReport.asPlainText(report, reportOnlyErrors));
      contentMap.put(
          "text/html; charset=\"us-ascii\"",
          healthCheckerReport.asHtml(u, report, reportOnlyErrors));

      try {
        emailHelper.sendEmail(c, subject, emailDestinations, smtpData, contentMap);
      } catch (MessagingException e) {
        log.warn("Health check had the following errors during mailing: " + e.getMessage());
        metricService.setStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_NOTIFICATION_STATUS, u),
            "Error sending Health check email: " + e.getMessage());
        return false;
      }
    }
    return true;
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (!running.compareAndSet(false, true)) {
      log.info("Previous run of health check scheduler is still underway");
      return;
    }

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
          log.error("Error running health check scheduler for customer " + c.uuid, ex);
        }
      }
    } catch (Exception e) {
      log.error("Error running health check scheduler", e);
    } finally {
      running.set(false);
    }
  }

  public void checkCustomer(Customer c) {
    // We need an alerting config to do work.
    CustomerConfig config = CustomerConfig.getAlertConfig(c.uuid);

    boolean shouldSendStatusUpdate = false;
    long storeIntervalMs = healthCheckStoreIntervalMs();
    AlertingData alertingData = null;
    long now = (new Date()).getTime();
    if (config != null && config.data != null) {
      alertingData = Json.fromJson(config.data, AlertingData.class);
      if (alertingData.checkIntervalMs > 0) {
        storeIntervalMs = alertingData.checkIntervalMs;
      }

      long statusUpdateIntervalMs =
          alertingData.statusUpdateIntervalMs <= 0
              ? statusUpdateIntervalMs()
              : alertingData.statusUpdateIntervalMs;
      shouldSendStatusUpdate =
          (now - statusUpdateIntervalMs) > lastStatusUpdateTimeMap.getOrDefault(c.uuid, 0L);
      if (shouldSendStatusUpdate) {
        lastStatusUpdateTimeMap.put(c.uuid, now);
      }
    }
    boolean onlyMetrics =
        !shouldSendStatusUpdate
            && ((now - storeIntervalMs) < lastCheckTimeMap.getOrDefault(c.uuid, 0L));

    if (!onlyMetrics) {
      lastCheckTimeMap.put(c.uuid, now);
    }
    checkAllUniverses(c, alertingData, shouldSendStatusUpdate, onlyMetrics);
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

    c.getUniverses()
        .stream()
        .map(
            u -> {
              String destinations = getAlertDestinations(u, c);
              return new CheckSingleUniverseParams(
                  u, c, shouldSendStatusUpdate, reportOnlyErrors, onlyMetrics, destinations);
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
      log.info("Shutting down Health Check thread pool");
      this.executor.shutdownNow();
    }

    return CompletableFuture.completedFuture(Done.done());
  }

  private static ExecutorService createExecutor(Config runtimeConfig) {
    // TODO: use YBThreadPoolExecutorFactory
    int numParallelism = getThreadpoolParallelism(runtimeConfig);

    // Initialize the health check thread pool.
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("Health-Check-Pool-%d").build();
    // Create an task pool which can handle an unbounded number of tasks, while using an initial
    // set of threads that get spawned up to TASK_THREADS limit.
    ExecutorService newExecutor = Executors.newFixedThreadPool(numParallelism, namedThreadFactory);

    log.info("Created Health Check thread pool");

    return newExecutor;
  }

  public CompletableFuture<Void> runHealthCheck(CheckSingleUniverseParams params) {
    String universeName = params.universe.name;
    CompletableFuture<Void> lastCheck = this.runningHealthChecks.get(params.universe.universeUUID);
    // Only schedule a task if the previous one for the given universe has completed.
    if (lastCheck != null && !lastCheck.isDone()) {
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
      log.warn("invalid universe config for disabled alerts: [ " + disabledUntilStr + " ]");
    }

    boolean silenceEmails = ((System.currentTimeMillis() / 1000) <= disabledUntilSecs);
    return silenceEmails ? null : String.join(",", destinations);
  }

  private static boolean isUniverseBusyByTask(UniverseDefinitionTaskParams details) {
    return details.updateInProgress
        && details.updatingTask != TaskType.BackupTable
        && details.updatingTask != TaskType.MultiTableBackup;
  }

  public void checkSingleUniverse(CheckSingleUniverseParams params) {
    // Validate universe data and make sure nothing is in progress.
    UniverseDefinitionTaskParams details = params.universe.getUniverseDetails();
    if (details == null) {
      log.warn("Skipping universe " + params.universe.name + " due to invalid details json...");
      setHealthCheckFailedMetric(
          params.customer, params.universe, "Health check skipped due to invalid details json.");
      return;
    }
    if (details.universePaused) {
      log.warn("Skipping universe " + params.universe.name + " as it is in the paused state...");
      return;
    }
    if (isUniverseBusyByTask(details)) {
      log.warn("Skipping universe " + params.universe.name + " due to task in progress...");
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
      info.enableYCQL = cluster.userIntent.enableYCQL;
      info.enableYEDIS = cluster.userIntent.enableYEDIS;
      if (cluster.userIntent.tserverGFlags.containsKey("ssl_protocols")) {
        info.sslProtocol = cluster.userIntent.tserverGFlags.get("ssl_protocols");
      }
      // Since health checker only uses CQLSH, we only care about the
      // client to node encryption flag.
      info.enableTls = cluster.userIntent.enableNodeToNodeEncrypt;
      info.enableTlsClient = cluster.userIntent.enableClientToNodeEncrypt;
      // Setting this flag to identify correct cert location.
      info.rootAndClientRootCASame = details.rootAndClientRootCASame;
      // Pass in whether YSQL authentication is enabled for the given cluster.
      info.enableYSQLAuth = cluster.userIntent.isYSQLAuthEnabled();

      Provider provider = Provider.get(UUID.fromString(cluster.userIntent.provider));
      if (provider == null) {
        log.warn(
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
          log.warn("Skipping universe " + params.universe.name + " due to invalid access key...");
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
            info.masterHttpPort = nd.masterHttpPort;
            info.tserverHttpPort = nd.tserverHttpPort;
            info.ysqlServerHttpPort = nd.ysqlServerHttpPort;
            break;
          }
        }
      }
      if (info.enableYCQL) {
        for (NodeDetails nd : details.nodeDetailsSet) {
          if (nd.isYqlServer) {
            info.ycqlPort = nd.yqlServerRpcPort;
            info.masterHttpPort = nd.masterHttpPort;
            info.tserverHttpPort = nd.tserverHttpPort;
            break;
          }
        }
      }

      if (info.enableYEDIS) {
        for (NodeDetails nd : details.nodeDetailsSet) {
          if (nd.isRedisServer) {
            info.redisPort = nd.redisServerRpcPort;
            break;
          }
        }
      }

      info.collectMetricsScript = generateMetricsCollectionScript(cluster);
    }

    // If any clusters were invalid, abort for this universe.
    if (invalidUniverseData) {
      return;
    }

    for (NodeDetails nd : details.nodeDetailsSet) {
      if (nd.cloudInfo.private_ip == null) {
        invalidUniverseData = true;
        log.warn(
            String.format(
                "Universe %s has unprovisioned node %s.", params.universe.name, nd.nodeName));
        setHealthCheckFailedMetric(
            params.customer,
            params.universe,
            String.format(
                "Can't run health check for the universe due to unprovisioned node%s.",
                nd.nodeName == null ? "" : " " + nd.nodeName));
        break;
      }

      HealthManager.ClusterInfo info = clusterMetadata.get(nd.placementUuid);
      if (info == null) {
        invalidUniverseData = true;
        log.warn(
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
    boolean shouldLogOutput =
        runtimeConfigFactory.forUniverse(params.universe).getBoolean("yb.health.logOutput");

    // Exit without calling script if the universe is in the "updating" state.
    // Doing the check before the Python script is executed.
    if (!canHealthCheckUniverse(params.universe.universeUUID)) {
      return;
    }

    // Call devops and process response.
    ShellResponse response =
        healthManager.runCommand(
            mainProvider,
            new ArrayList<>(clusterMetadata.values()),
            potentialStartTime,
            shouldLogOutput);

    // Checking the interruption necessity after the Python script finished.
    // It is not needed to analyze results if the universe has the "update in
    // progress" state.
    if (!canHealthCheckUniverse(params.universe.universeUUID)) {
      return;
    }

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
              params.reportOnlyErrors,
              params.onlyMetrics);

      if (!params.onlyMetrics) {
        HealthCheck.addAndPrune(
            params.universe.universeUUID, params.universe.customerId, response.message);
      }

      if (succeeded) {
        metricService.setOkStatusMetric(
            buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, params.universe));
      }
    } else {
      log.error(
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
    // Remove old metrics and create only health check failed.
    MetricFilter toClean = metricSourceKeysFilter(customer, universe, HEALTH_CHECK_METRICS);
    Metric healthCheckFailed =
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, universe)
            .setLabel(KnownAlertLabels.ERROR_MESSAGE, message)
            .setValue(0.0);
    metricService.cleanAndSave(Collections.singletonList(healthCheckFailed), toClean);
  }

  private String generateMetricsCollectionScript(Cluster cluster) {
    String template;
    try (InputStream templateStream =
        environment.resourceAsStream("metric/collect_metrics.sh.template")) {
      template = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
      // For now it has no universe/cluster specific info. Add placeholder substitution here once
      // they are added.
      Path path = Paths.get("/tmp/collect_metrics_" + cluster.uuid + ".sh");

      Files.write(path, template.getBytes(StandardCharsets.UTF_8));

      return path.toString();
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition rule template", e);
    }
  }

  private MetricFilter metricSourceKeysFilter(
      Customer customer, Universe universe, List<PlatformMetrics> metrics) {
    List<MetricSourceKey> metricSourceKeys =
        metrics
            .stream()
            .map(
                nodeMetric ->
                    MetricSourceKey.builder()
                        .customerUuid(customer.getUuid())
                        .name(nodeMetric.getMetricName())
                        .sourceUuid(universe.getUniverseUUID())
                        .build())
            .collect(Collectors.toList());
    return MetricFilter.builder().sourceKeys(metricSourceKeys).build();
  }

  @VisibleForTesting
  static boolean canHealthCheckUniverse(UUID universeUUID) {
    Optional<Universe> u = Universe.maybeGet(universeUUID);
    UniverseDefinitionTaskParams universeDetails =
        u.isPresent() ? u.get().getUniverseDetails() : null;
    if (universeDetails == null) {
      log.warn(
          "Cancelling universe "
              + universeUUID
              + " health-check, the universe not found or empty universe details.");
      return false;
    }

    if (isUniverseBusyByTask(universeDetails)) {
      log.warn("Cancelling universe " + u.get().name + " health-check, some task is in progress.");
      return false;
    }
    return true;
  }
}
