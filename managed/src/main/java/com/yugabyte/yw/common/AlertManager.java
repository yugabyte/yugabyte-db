/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverInterface;
import com.yugabyte.yw.common.alerts.AlertReceiverManager;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.YWValidateException;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.Alert.State;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import javax.inject.Inject;
import javax.inject.Singleton;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

@Singleton
public class AlertManager {

  public static final Logger LOG = LoggerFactory.getLogger(AlertManager.class);

  @VisibleForTesting static final String ALERT_MANAGER_ERROR_CODE = "ALERT_MANAGER_FAILURE";

  public static final UUID DEFAULT_ALERT_RECEIVER_UUID = new UUID(0, 0);

  @VisibleForTesting static final UUID DEFAULT_ALERT_ROUTE_UUID = new UUID(0, 0);

  @Inject private EmailHelper emailHelper;

  @Inject private AlertReceiverManager receiversManager;

  /**
   * A method to run a state transition for a given alert
   *
   * @param alert the alert to transition states on
   * @return the alert in a new state
   */
  public Alert transitionAlert(Alert alert) {
    try {
      switch (alert.getState()) {
        case CREATED:
          LOG.info("Transitioning alert {} to active", alert.getUuid());
          alert.setState(Alert.State.ACTIVE);
          sendNotification(alert);
          break;
        case ACTIVE:
          LOG.info("Transitioning alert {} to resolved (with email)", alert.getUuid());
          alert.setState(Alert.State.RESOLVED);
          sendNotification(alert);
          break;
        case RESOLVED:
          LOG.info("Transitioning alert {} to resolved (no email)", alert.getUuid());
          alert.setState(Alert.State.RESOLVED);
          break;
      }

      alert.save();
    } catch (Exception e) {
      LOG.error("Error transitioning alert state for alert {}", alert.getUuid(), e);
    }

    return alert;
  }

  /**
   * Updates states of all active alerts (according to criteria) to RESOLVED.
   *
   * @param customerUUID
   * @param targetUUID
   * @param errorCode Error code string (LIKE wildcards allowed)
   */
  public void resolveAlerts(UUID customerUUID, UUID targetUUID, String errorCode) {
    List<Alert> activeAlerts =
        Alert.list(customerUUID, errorCode, targetUUID)
            .stream()
            .filter(alert -> alert.getState() == State.ACTIVE || alert.getState() == State.CREATED)
            .collect(Collectors.toList());
    LOG.debug("Resetting alerts for '{}', count {}", errorCode, activeAlerts.size());
    for (Alert alert : activeAlerts) {
      alert.setState(State.RESOLVED);
      alert.save();
    }
  }

  private void createAlert(Customer c, Alert.TargetType type, UUID uuid, String details) {
    // We don't check the target type while searching for already existing alerts as
    // uuid is enough for this.
    if (Alert.getActiveCustomerAlertsByTargetUuid(c.uuid, uuid).size() == 0) {
      Alert.create(
          c.uuid,
          uuid,
          type, // Alert.TargetType.CustomerConfigType,
          ALERT_MANAGER_ERROR_CODE,
          "Error",
          details);
    }
  }

  public void sendNotification(Alert alert) {
    Customer customer = Customer.get(alert.customerUUID);

    List<AlertRoute> routes =
        alert.getDefinitionUUID() == null
            ? new ArrayList<>()
            : AlertRoute.listByDefinition(alert.getDefinitionUUID());

    if (routes.isEmpty()) {
      // Creating default route with email only, w/o saving it to DB.
      LOG.debug("For alert {} no routes found, using default email.", alert.getUuid());
      if (!alert.sendEmail) {
        return;
      }
      routes.add(getDefaultRoute(alert));
    }

    for (AlertRoute route : routes) {
      AlertReceiver receiver =
          route.getReceiverUUID() == DEFAULT_ALERT_RECEIVER_UUID
              ? getDefaultReceiver(alert.customerUUID)
              : AlertReceiver.get(alert.customerUUID, route.getReceiverUUID());
      try {
        AlertUtils.validate(receiver);
      } catch (YWValidateException e) {
        LOG.warn("Route {} skipped: {}", route.getUuid(), e.getMessage(), e);
        continue;
      }

      try {
        AlertReceiverInterface handler =
            receiversManager.get(AlertUtils.getJsonTypeName(receiver.getParams()));
        handler.sendNotification(customer, alert, receiver);
        resolveAlerts(customer.uuid, receiver.getUuid(), ALERT_MANAGER_ERROR_CODE);

        if (!receiver.getParams().continueSend) {
          break;
        }
      } catch (Exception e) {
        LOG.error(e.getMessage());
        createAlert(
            customer,
            Alert.TargetType.AlertReceiverType,
            receiver.getUuid(),
            "Error sending notification: " + e);
      }
    }
  }

  private AlertReceiver getDefaultReceiver(UUID customerUUID) {
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.recipients = emailHelper.getDestinations(customerUUID);
    params.smtpData = emailHelper.getSmtpData(customerUUID);

    AlertReceiver defaultReceiver = new AlertReceiver();
    defaultReceiver.setUuid(DEFAULT_ALERT_RECEIVER_UUID);
    defaultReceiver.setCustomerUuid(customerUUID);
    defaultReceiver.setParams(params);
    return defaultReceiver;
  }

  private AlertRoute getDefaultRoute(Alert alert) {
    LOG.debug("Creating default route.");
    AlertRoute route = new AlertRoute();
    route.setUUID(DEFAULT_ALERT_ROUTE_UUID);
    route.setDefinitionUUID(alert.getDefinitionUUID());
    route.setReceiverUUID(DEFAULT_ALERT_RECEIVER_UUID);
    return route;
  }
}
