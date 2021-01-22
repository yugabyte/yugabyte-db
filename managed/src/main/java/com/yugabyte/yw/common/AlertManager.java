/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;

@Singleton
public class AlertManager {
  @Inject
  HealthManager healthManager;

  @Inject
  RuntimeConfigFactory runtimeConfigFactory;


  public static final Logger LOG = LoggerFactory.getLogger(AlertManager.class);

  public void sendEmail(Alert alert, String state) {
    if (!alert.sendEmail) {
      return;
    }

    AlertDefinition definition = AlertDefinition.get(alert.definitionUUID);
    Universe universe = Universe.get(definition.universeUUID);
    ObjectNode alertData = Json.newObject()
      .put("alert_name", definition.name)
      .put("state", state)
      .put("universe_name", universe.name);
    Customer customer = Customer.get(alert.customerUUID);
    String customerTag = String.format("[%s][%s]", customer.name, customer.code);
    List<String> destinations = new ArrayList<>();
    CustomerConfig config = CustomerConfig.getAlertConfig(customer.uuid);
    CustomerRegisterFormData.AlertingData alertingData =
      Json.fromJson(config.data, CustomerRegisterFormData.AlertingData.class);
    if (alertingData.sendAlertsToYb &&
      runtimeConfigFactory.staticApplicationConf().hasPath("yb.health.default_email")) {
      String ybEmail = runtimeConfigFactory
        .staticApplicationConf()
        .getString("yb.health.default_email");
      if (!ybEmail.isEmpty()) {
        destinations.add(ybEmail);
      }
    }

    if (alertingData.alertingEmail != null && !alertingData.alertingEmail.isEmpty()) {
      destinations.add(alertingData.alertingEmail);
    }

    // Skip sending email if there aren't any destinations to send it to
    if (destinations.isEmpty()) {
      return;
    }

    CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customer.uuid);
    CustomerRegisterFormData.SmtpData smtpData = null;
    if (smtpConfig != null) {
      smtpData = Json.fromJson(smtpConfig.data, CustomerRegisterFormData.SmtpData.class);
    }

    healthManager.runCommand(
      customerTag,
      String.join(",", destinations),
      smtpData,
      alertData
    );
  }

  /**
   * A method to run a state transition for a given alert
   *
   * @param alert the alert to transition states on
   * @return the alert in a new state
   */
  public Alert transitionAlert(Alert alert) {
    try {
      switch (alert.state) {
        case CREATED:
          LOG.info("Transitioning alert {} to active", alert.uuid);
          sendEmail(alert, "firing");
          alert.state = Alert.State.ACTIVE;
          break;
        case ACTIVE:
          LOG.info("Transitioning alert {} to resolved", alert.uuid);
          sendEmail(alert, "resolved");
          alert.state = Alert.State.RESOLVED;
          break;
        case RESOLVED:
          LOG.info("Transitioning alert {} to resolved", alert.uuid);
          alert.state = Alert.State.RESOLVED;
          break;
      }

      alert.save();
    } catch (Exception e) {
      LOG.error("Error transitioning alert state for alert {}", alert.uuid, e);
    }

    return alert;
  }
}
