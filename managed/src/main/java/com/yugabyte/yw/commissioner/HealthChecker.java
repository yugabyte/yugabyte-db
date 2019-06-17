// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.Date;
import java.util.HashMap;
import java.util.stream.Collectors;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.google.inject.Inject;
import com.google.inject.Singleton;

import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.HealthManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.api.Play;
import play.Configuration;
import play.libs.Json;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.TimeUnit;
import play.Environment;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
public class HealthChecker {
  public static final Logger LOG = LoggerFactory.getLogger(HealthChecker.class);

  play.Configuration config;

  private long HEALTH_CHECK_SLEEP_INTERVAL_MS = 0;

  private long STATUS_UPDATE_INTERVAL_MS = 0;

  // Last time we sent a status update email per customer.
  private Map<UUID, Long> lastStatusUpdateTimeMap = new HashMap<>();

  // Last time we actually ran the health check script per customer.
  private Map<UUID, Long> lastCheckTimeMap = new HashMap<>();

  // What will run the health checking script.
  HealthManager healthManager;

  private AtomicBoolean running = new AtomicBoolean(false);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final Environment environment;

  @Inject
  public HealthChecker(
      ActorSystem actorSystem,
      Configuration config,
      Environment environment,
      ExecutionContext executionContext,
      HealthManager healthManager) {
    this.actorSystem = actorSystem;
    this.config = config;
    this.environment = environment;
    this.executionContext = executionContext;
    this.healthManager = healthManager;

    this.initialize();
  }

  private void initialize() {
    this.actorSystem.scheduler().schedule(
      Duration.create(0, TimeUnit.MILLISECONDS), // initialDelay
      Duration.create(this.healthCheckIntervalMs(), TimeUnit.MILLISECONDS), // interval
      () -> scheduleRunner(),
      this.executionContext
    );
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

  private String ybAlertEmail() {
    return config.getString("yb.health.default_email");
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (running.get()) {
      LOG.info("Previous run still underway");
      return;
    }

    LOG.info("Running health checker");
    running.set(true);
    // TODO(bogdan): This will not be too DB friendly when we go multi-tenant.
    for (Customer c : Customer.getAll()) {
      checkCustomer(c);
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
    long checkIntervalMs = alertingData.checkIntervalMs <= 0
      ? healthCheckIntervalMs()
      : alertingData.checkIntervalMs;
    boolean shouldRunCheck = (now - checkIntervalMs) >
        lastCheckTimeMap.getOrDefault(c.uuid, 0l);
    long statusUpdateIntervalMs = alertingData.statusUpdateIntervalMs <= 0
      ? statusUpdateIntervalMs()
      : alertingData.statusUpdateIntervalMs;
    boolean shouldSendStatusUpdate = (now - statusUpdateIntervalMs) >
        lastStatusUpdateTimeMap.getOrDefault(c.uuid, 0l);
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

  public void checkAllUniverses(
      Customer c, CustomerConfig config, boolean shouldSendStatusUpdate) {
    // Process all of a customer's universes.
    for (Universe u : c.getUniverses()) {
      checkSingleUniverse(u, c, config, shouldSendStatusUpdate);
    }
  }

  public void checkSingleUniverse(
      Universe u, Customer c, CustomerConfig config, boolean shouldSendStatusUpdate) {
    // Validate universe data and make sure nothing is in progress.
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    if (details == null) {
      LOG.warn("Skipping universe " + u.name + " due to invalid details json...");
      return;
    }
    if (details.updateInProgress) {
      LOG.warn("Skipping universe " + u.name + " due to task in progress...");
      return;
    }
    LOG.info("Doing health check for universe: " + u.name);
    Map<UUID, HealthManager.ClusterInfo> clusterMetadata = new HashMap<>();
    boolean invalidUniverseData = false;
    String providerCode = "";
    for (UniverseDefinitionTaskParams.Cluster cluster : details.clusters) {
      HealthManager.ClusterInfo info = new HealthManager.ClusterInfo();
      clusterMetadata.put(cluster.uuid, info);
      info.ybSoftwareVersion = cluster.userIntent.ybSoftwareVersion;
      info.enableYSQL = cluster.userIntent.enableYSQL;
      Provider provider = Provider.get(UUID.fromString(cluster.userIntent.provider));
      if (provider == null) {
        LOG.warn("Skipping universe " + u.name + " due to invalid provider "
            + cluster.userIntent.provider);
        invalidUniverseData = true;
        break;
      }
      providerCode = provider.code;
      if (providerCode.equals(Common.CloudType.kubernetes.toString())) {
        info.namespaceToConfig = PlacementInfoUtil.getConfigPerNamespace(
            cluster.placementInfo, details.nodePrefix);
      }

      // TODO(bogdan): We do not have access to the default port constant at this level, as it is
      // baked in devops...hardcode it for now.
      // Default to 54422.
      int sshPort = 54422;
      if (providerCode.equals(Common.CloudType.onprem.toString())) {
        // For onprem, technically, each node could have a different port, but let's pick one of
        // the nodes for now and go from there.
        // TODO(bogdan): Improve the onprem support in devops to have port per node.
        for (NodeDetails nd : details.nodeDetailsSet) {
          NodeInstance onpremNode = NodeInstance.getByName(nd.nodeName);
          if (onpremNode != null) {
            NodeInstanceFormData.NodeInstanceData onpremDetails = onpremNode.getDetails();
            if (onpremDetails != null) {
              sshPort = onpremDetails.sshPort;
              break;
            }
          }
        }
      }
      AccessKey accessKey = AccessKey.get(provider.uuid, cluster.userIntent.accessKeyCode);
      if (accessKey == null || accessKey.getKeyInfo() == null) {
        if (!providerCode.equals(CloudType.kubernetes.toString())) {
          LOG.warn("Skipping universe " + u.name + " due to invalid access key...");
          invalidUniverseData = true;
          break;
        }
      } else {
        info.identityFile = accessKey.getKeyInfo().privateKey;
      }
      info.sshPort = sshPort;
      if (info.enableYSQL) {
        for (NodeDetails nd : details.nodeDetailsSet) {
          if (nd.isYsqlServer) {
           info.ysqlPort = nd.ysqlServerRpcPort;
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
      HealthManager.ClusterInfo info = clusterMetadata.get(nd.placementUuid);
      if (info == null) {
        invalidUniverseData = true;
        LOG.warn(String.format(
              "Universe %s has node %s with invalid placement %s", u.name, nd.nodeName,
              nd.placementUuid));
        break;
      }
      // TODO: we do not have a good way of marking the whole universe as k8s only.
      if (nd.isMaster) {
        info.masterNodes.add(nd.cloudInfo.private_ip);
      }
      if (nd.isTserver) {
        info.tserverNodes.add(nd.cloudInfo.private_ip);
      }
    }
    // If any nodes were invalid, abort for this universe.
    if (invalidUniverseData) {
      return;
    }
    List<String> destinations = new ArrayList<String>();
    if (config != null) {
      AlertingData alertingData = Json.fromJson(config.data, AlertingData.class);
      String ybEmail = ybAlertEmail();
      if (alertingData.sendAlertsToYb && ybEmail != null && !ybEmail.isEmpty()) {
        destinations.add(ybEmail);
      }
      if (alertingData.alertingEmail != null && !alertingData.alertingEmail.isEmpty()) {
        destinations.add(alertingData.alertingEmail);
      }
    }
    CustomerTask lastTask = CustomerTask.getLatestByUniverseUuid(u.universeUUID);
    long potentialStartTime = 0;
    if (lastTask != null && lastTask.getCompletionTime()!= null) {
      potentialStartTime = lastTask.getCompletionTime().getTime();
    }
    // If last check had errors, set the flag to send an email. If this check will have an error,
    // we would send an email anyway, but if this check shows a healthy universe, let's send an
    // email about it.
    HealthCheck lastCheck = HealthCheck.getLatest(u.universeUUID);
    boolean lastCheckHadErrors = lastCheck != null && lastCheck.hasError();
    // Setup customer tag including email and code, for ease of email parsing.
    String customerTag = String.format("[%s][%s]", c.email, c.code);
    Provider mainProvider = Provider.get(UUID.fromString(
          details.getPrimaryCluster().userIntent.provider));
    // Call devops and process response.
    ShellProcessHandler.ShellResponse response = healthManager.runCommand(
        mainProvider,
        new ArrayList(clusterMetadata.values()),
        u.name,
        customerTag,
        (destinations.size() == 0 ? null : String.join(",", destinations)),
        potentialStartTime,
        (shouldSendStatusUpdate || lastCheckHadErrors));
    if (response.code == 0) {
      HealthCheck.addAndPrune(u.universeUUID, u.customerId, response.message);
    } else {
      LOG.error(String.format(
          "Health check script got error: code (%s), msg: ", response.code, response.message));
    }
  }
}
