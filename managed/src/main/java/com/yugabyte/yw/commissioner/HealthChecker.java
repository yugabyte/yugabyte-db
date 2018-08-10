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
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.api.Play;
import play.libs.Json;

@Singleton
public class HealthChecker extends Thread {
  public static final Logger LOG = LoggerFactory.getLogger(HealthChecker.class);

  play.Configuration appConfig;

  // Email address from YugaByte to which to send emails, if enabled.
  private String YB_ALERT_EMAIL = null;

  // The interval at which the checker will run.
  // Can be overridden per customer.
  private long HEALTH_CHECK_SLEEP_INTERVAL_MS = 0;

  // The interval at which to send a status update of all the current universes.
  // Can be overridden per customer.
  private long STATUS_UPDATE_INTERVAL_MS = 0;

  // Last time we sent a status update email per customer.
  private Map<UUID, Long> lastStatusUpdateTimeMap = new HashMap<>();

  // Last time we actually ran the health check script per customer.
  private Map<UUID, Long> lastCheckTimeMap = new HashMap<>();

  // What will run the health checking script.
  static HealthManager healthManager = null;

  public HealthChecker() {
    setName("HealthChecker");
  }

  @Override
  public void run() {
    LOG.info("Starting health checking");
    while (true) {
      try {
        if (healthManager == null) {
          initHealthManager();
        }
      } catch (RuntimeException e) {
        LOG.error("Could not setup HealthManager yet...");
        healthManager = null;
      }
      if (healthManager != null) {
        // TODO(bogdan): This will not be too DB friendly when we go multi-tenant.
        for (Customer c : Customer.getAll()) {
          CustomerConfig config = CustomerConfig.getAlertConfig(c.uuid);
          if (config == null) {
            // LOG.debug("Skipping customer " + c.uuid + " due to missing alerting config...");
            continue;
          }
          AlertingData alertingData = Json.fromJson(config.data, AlertingData.class);
          long now = (new Date()).getTime();
          long checkIntervalMs = alertingData.checkIntervalMs <= 0
            ? HEALTH_CHECK_SLEEP_INTERVAL_MS
            : alertingData.checkIntervalMs;
          boolean shouldRunCheck = (now - checkIntervalMs) >
              lastCheckTimeMap.getOrDefault(c.uuid, 0l);
          long statusUpdateIntervalMs = alertingData.statusUpdateIntervalMs <= 0
            ? STATUS_UPDATE_INTERVAL_MS
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
      }
      sleep();
    }
  }

  /*
   * This function will error out while the App is not yet initialized. Once it is, we should be
   * safe to read the appConfig values.
   */
  private void initHealthManager() {
    healthManager = Play.current().injector().instanceOf(HealthManager.class);
    appConfig = Play.current().injector().instanceOf(play.Configuration.class);

    HEALTH_CHECK_SLEEP_INTERVAL_MS = appConfig.getLong("yb.health.check_interval_ms");
    STATUS_UPDATE_INTERVAL_MS = appConfig.getLong("yb.health.status_interval_ms");
    YB_ALERT_EMAIL = appConfig.getString("yb.health.default_email");
  }

  private void checkAllUniverses(Customer c, CustomerConfig config,
      boolean shouldSendStatusUpdate) {
    for (Universe u : c.getUniverses()) {
      UniverseDefinitionTaskParams details = u.getUniverseDetails();
      if (details == null) {
        LOG.warn("Skipping universe " + u.name + " due to invalid details json...");
        continue;
      }
      if (details.updateInProgress) {
        LOG.warn("Skipping universe " + u.name + " due to task in progress...");
        continue;
      }
      LOG.info("Doing health check for universe: " + u.name);
      Map<UUID, HealthManager.ClusterInfo> clusterMetadata = new HashMap<>();
      boolean invalidUniverseData = false;
      for (UniverseDefinitionTaskParams.Cluster cluster : details.clusters) {
        HealthManager.ClusterInfo info = new HealthManager.ClusterInfo();

        AccessKey accessKey = AccessKey.get(
            UUID.fromString(cluster.userIntent.provider),
            cluster.userIntent.accessKeyCode);
        if (accessKey == null || accessKey.getProviderUUID() == null) {
          LOG.warn("Skipping universe " + u.name + " due to invalid access key...");
          continue;
        }
        String providerCode = Provider.get(accessKey.getProviderUUID()).code;
        // TODO(bogdan): We do not have access to the default port constnat at this level, as it is
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
        info.sshPort = sshPort;
        info.identityFile = accessKey.getKeyInfo().privateKey;
        clusterMetadata.put(cluster.uuid, info);
      }
      for (NodeDetails nd : details.nodeDetailsSet) {
        HealthManager.ClusterInfo cluster = clusterMetadata.get(nd.placementUuid);
        if (cluster == null) {
          invalidUniverseData = true;
          LOG.warn(String.format(
                "Universe %s has node %s with invalid placement %s", u.name, nd.nodeName,
                nd.placementUuid));
          break;
        }
        if (nd.isMaster) {
          cluster.masterNodes.add(nd.cloudInfo.private_ip);
        }
        if (nd.isTserver) {
          cluster.tserverNodes.add(nd.cloudInfo.private_ip);
        }
      }
      // If any nodes were invalid, abort for this universe.
      if (invalidUniverseData) {
        continue;
      }
      List<String> destinations = new ArrayList<String>();
      if (config != null) {
        AlertingData alertingData = Json.fromJson(config.data, AlertingData.class);
        if (alertingData.sendAlertsToYb) {
          destinations.add(YB_ALERT_EMAIL);
        }
        if (alertingData.alertingEmail != null && !alertingData.alertingEmail.isEmpty()) {
          destinations.add(alertingData.alertingEmail);
        }
      }
      String customerTag = String.format("[%s][%s]", c.email, c.code);
      ShellProcessHandler.ShellResponse response = healthManager.runCommand(
          new ArrayList(clusterMetadata.values()), u.name, customerTag,
          (destinations.size() == 0 ? null : String.join(",", destinations)),
          shouldSendStatusUpdate);
      if (response.code == 0) {
        HealthCheck.addAndPrune(u.universeUUID, u.customerId, response.message);
      } else {
        LOG.error(String.format(
            "Health check script got error: code (%s), msg: ", response.code, response.message));
      }
    }
  }

  private void sleep() {
    // Sleep for 5 seconds and check if it is time to run the script.
    try {
      Thread.sleep(5000);
    } catch (InterruptedException e) {
    }
  }
}
