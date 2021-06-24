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
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Gauge;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.inject.ApplicationLifecycle;
import play.libs.Json;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

import javax.mail.MessagingException;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

@Singleton
public class HealthChecker {
  public static final Logger LOG = LoggerFactory.getLogger(HealthChecker.class);

  public static final String kUnivMetricName = "yb_univ_health_status";
  public static final String kUnivUUIDLabel = "univ_uuid";
  public static final String kUnivNameLabel = "univ_name";
  public static final String kCheckLabel = "check_name";
  public static final String kNodeLabel = "node";

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

  private final AlertService alertService;

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
      AlertService alertService,
      RuntimeConfigFactory runtimeConfigFactory,
      ApplicationLifecycle lifecycle) {
    this.actorSystem = actorSystem;
    this.config = config;
    this.executionContext = executionContext;
    this.healthManager = healthManager;
    this.promRegistry = promRegistry;
    this.healthCheckerReport = healthCheckerReport;
    this.emailHelper = emailHelper;
    this.alertService = alertService;
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
      AlertService alertService,
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
        alertService,
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

  private void processResults(
      Customer c,
      Universe u,
      String response,
      long durationMs,
      String emailDestinations,
      boolean sendMailAlways,
      boolean reportOnlyErrors) {

    JsonNode healthJSON = null;
    try {
      healthJSON = Util.convertStringToJson(response);
    } catch (Exception e) {
      LOG.warn("Failed to convert health check response to JSON " + e.getMessage());
      createAlert(c, u, "Error converting health check response to JSON: " + e.getMessage());
    }

    if (healthJSON != null) {
      boolean hasErrors = false;
      try {
        for (JsonNode entry : healthJSON.path("data")) {
          String nodeName = entry.path("node").asText();
          String checkName = entry.path("message").asText();
          boolean checkResult = entry.path("has_error").asBoolean();
          hasErrors = checkResult || hasErrors;
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

        if (!hasErrors) {
          AlertFilter filter =
              AlertFilter.builder()
                  .customerUuid(c.getUuid())
                  .errorCode(KnownAlertCodes.HEALTH_CHECKER_FAILURE)
                  .label(KnownAlertLabels.TARGET_UUID, u.universeUUID.toString())
                  .build();
          alertService.markResolved(filter);
        }

      } catch (Exception e) {
        LOG.warn("Failed to convert health check response to prometheus metrics " + e.getMessage());
        createAlert(
            c,
            u,
            "Error converting health check response to prometheus metrics: " + e.getMessage());
      }

      SmtpData smtpData = emailHelper.getSmtpData(c.uuid);
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
          createAlert(c, u, "Error sending Health check email: " + mailError);
        }
      }
    }
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

  private void createAlert(Customer c, Universe u, String details) {
    Alert alert =
        new Alert()
            .setCustomerUUID(c.getUuid())
            .setErrCode(KnownAlertCodes.HEALTH_CHECKER_FAILURE)
            .setType(KnownAlertTypes.Warning)
            .setMessage(details)
            .setSendEmail(true)
            .setLabels(AlertDefinitionLabelsBuilder.create().appendTarget(u).getAlertLabels());
    alertService.create(alert);
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
                createAlert(
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
      createAlert(
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
        createAlert(
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
          createAlert(
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
        createAlert(
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
        createAlert(params.customer, params.universe, alertText);

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

    // Call devops and process response.
    ShellResponse response =
        healthManager.runCommand(
            mainProvider, new ArrayList<>(clusterMetadata.values()), potentialStartTime);

    long durationMs = System.currentTimeMillis() - startMs;
    boolean sendMailAlways = (params.shouldSendStatusUpdate || lastCheckHadErrors);

    if (response.code == 0) {
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
      createAlert(params.customer, params.universe, alertText);
    }
  }
}
