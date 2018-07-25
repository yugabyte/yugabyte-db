// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.Date;
import java.util.stream.Collectors;
import java.util.List;
import java.util.UUID;

import com.google.inject.Inject;
import com.google.inject.Singleton;

import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.HealthManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.api.Play;
import play.libs.Json;

@Singleton
public class HealthChecker extends Thread {
  public static final Logger LOG = LoggerFactory.getLogger(HealthChecker.class);

  play.Configuration appConfig;

  // The interval after which progress monitor wakes up and does work.
  private long HEALTH_CHECK_SLEEP_INTERVAL_MS = 0;

  // Email address from YugaByte to which to send emails, if configured.
  private String YB_ALERT_EMAIL = null;

  // The interval at which to send a status update of all the current universes.
  private long STATUS_UPDATE_INTERVAL_MS = 0;

  // Last time we send a status update email, in ms.
  private long lastStatusUpdateTime = 0;

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
        checkAllUniverses();
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

  /*
   * Check if we need to send a status update email. Also update the internal tracking of last time
   * we checked to now.
   */
  private boolean checkForStatusUpdate() {
    long now = (new Date()).getTime();
    boolean shouldSendStatusUpdate = (now - STATUS_UPDATE_INTERVAL_MS) > lastStatusUpdateTime;
    if (shouldSendStatusUpdate) {
      lastStatusUpdateTime = now;
    }
    return shouldSendStatusUpdate;
  }

  private void checkAllUniverses() {
    boolean shouldSendStatusUpdate = checkForStatusUpdate();

    for (Customer c : Customer.getAll()) {
      CustomerConfig config = CustomerConfig.getAlertConfig(c.uuid);
      if (config == null) {
        // LOG.debug("Skipping customer " + c.uuid + " due to missing alerting config...");
        continue;
      }
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
        // TODO: this should ignore Stopped nodes?
        String masterNodes = details.nodeDetailsSet.stream()
            .filter(nd -> nd.isMaster)
            .map(nd -> nd.cloudInfo.private_ip)
            .collect(Collectors.joining(","));
        String tserverNodes = details.nodeDetailsSet.stream()
            .filter(nd -> nd.isTserver)
            .map(nd -> nd.cloudInfo.private_ip)
            .collect(Collectors.joining(","));
        UniverseDefinitionTaskParams.Cluster primaryCluster = details.getPrimaryCluster();
        AccessKey accessKey = AccessKey.get(
            UUID.fromString(primaryCluster.userIntent.provider),
            primaryCluster.userIntent.accessKeyCode);
        if (accessKey == null || accessKey.getProviderUUID() == null) {
          LOG.warn("Skipping universe " + u.name + " due to invalid access key...");
          continue;
        }
        String providerCode = Provider.get(accessKey.getProviderUUID()).code;
        // TODO: we do not really have the ssh ports at this layer..
        String sshPort = providerCode.equals(Common.CloudType.onprem) ? "22" : "54422";
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
        ShellProcessHandler.ShellResponse response = healthManager.runCommand(
            masterNodes, tserverNodes, sshPort, u.name, accessKey.getKeyInfo().privateKey,
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
  }

  private void sleep() {
    // Sleep for the required interval.
    try {
      // If the fields have not yet been initialized from the appConfig, then take a short sleep.
      Thread.sleep(HEALTH_CHECK_SLEEP_INTERVAL_MS > 0 ? HEALTH_CHECK_SLEEP_INTERVAL_MS : 1000);
    } catch (InterruptedException e) {
    }
  }
}
