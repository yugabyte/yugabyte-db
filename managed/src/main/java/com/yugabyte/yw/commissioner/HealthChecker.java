// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import java.util.List;
import java.util.UUID;

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

public class HealthChecker extends Thread {
  public static final Logger LOG = LoggerFactory.getLogger(HealthChecker.class);

  // The interval after which progress monitor wakes up and does work.
  private final long HEALTH_CHECK_SLEEP_INTERVAL_MS = 300000;

  // TODO: make this a global config.
  public final String YB_ALERT_EMAIL = "nosuchemail@example.com";

  private AtomicBoolean shuttingDown;
  // What will run the health checking script.
  static HealthManager healthManager = null;

  public HealthChecker(AtomicBoolean shuttingDown) {
    this.shuttingDown = shuttingDown;
    setName("HealthChecker");
  }

  @Override
  public void run() {
    LOG.info("Starting health checking");
    while (!shuttingDown.get()) {
      try {
        if (healthManager == null) {
          healthManager = Play.current().injector().instanceOf(HealthManager.class);
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

  private void checkAllUniverses() {
    for (Customer c : Customer.getAll()) {
      CustomerConfig config = CustomerConfig.getAlertConfig(c.uuid);
      if (config == null) {
        LOG.debug("Skipping customer " + c.uuid + " due to missing alerting config...");
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
        String nodes = details.nodeDetailsSet.stream()
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
            nodes, sshPort, u.name, accessKey.getKeyInfo().privateKey,
            (destinations.size() == 0 ? null : String.join(",", destinations)));
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
      Thread.sleep(HEALTH_CHECK_SLEEP_INTERVAL_MS);
    } catch (InterruptedException e) {
    }
  }
}
