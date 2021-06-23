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
import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Singleton
public class AlertManager {

  private static final Logger LOG = LoggerFactory.getLogger(AlertManager.class);

  private final EmailHelper emailHelper;
  private final AlertService alertService;
  private final AlertReceiverManager receiversManager;

  @Inject
  public AlertManager(
      EmailHelper emailHelper, AlertService alertService, AlertReceiverManager receiversManager) {
    this.emailHelper = emailHelper;
    this.alertService = alertService;
    this.receiversManager = receiversManager;
  }

  public static final UUID DEFAULT_ALERT_RECEIVER_UUID = new UUID(0, 0);

  @VisibleForTesting static final UUID DEFAULT_ALERT_ROUTE_UUID = new UUID(0, 0);

  /**
   * A method to run a state transition for a given alert
   *
   * @param alert the alert to transition states on
   * @return the alert in a new state
   */
  public Alert transitionAlert(Alert alert, AlertNotificationReport report) {
    try {
      switch (alert.getState()) {
        case CREATED:
          LOG.info("Transitioning alert {} to active", alert.getUuid());
          report.raiseAttempt();
          alert.setState(Alert.State.ACTIVE);
          sendNotification(alert, report);
          break;
        case ACTIVE:
          LOG.info("Transitioning alert {} to resolved (with email)", alert.getUuid());
          report.resolveAttempt();
          alert.setState(Alert.State.RESOLVED);
          sendNotification(alert, report);
          break;
        default:
          LOG.warn(
              "Unexpected alert state {} during notification for alert {}",
              alert.getState().name(),
              alert.getUuid());
      }

      alert.save();
    } catch (Exception e) {
      report.failAttempt();
      LOG.error("Error transitioning alert state for alert {}", alert.getUuid(), e);
    }

    return alert;
  }

  public void sendNotification(Alert alert, AlertNotificationReport report) {
    Customer customer = Customer.get(alert.getCustomerUUID());

    List<AlertRoute> routes =
        alert.getDefinitionUUID() == null
            ? new ArrayList<>()
            : AlertRoute.listByDefinition(alert.getDefinitionUUID());

    if (routes.isEmpty()) {
      // Creating default route with email only, w/o saving it to DB.
      LOG.debug("For alert {} no routes found, using default email.", alert.getUuid());
      if (!alert.isSendEmail()) {
        return;
      }
      routes.add(getDefaultRoute(alert));
    }

    boolean atLeastOneSucceeded = false;
    for (AlertRoute route : routes) {
      AlertReceiver receiver =
          route.getReceiverUUID() == DEFAULT_ALERT_RECEIVER_UUID
              ? getDefaultReceiver(alert.getCustomerUUID())
              : AlertReceiver.get(alert.getCustomerUUID(), route.getReceiverUUID());
      try {
        AlertUtils.validate(receiver);
      } catch (YWValidateException e) {
        if (report.failuresByReceiver(receiver.getUuid()) == 0) {
          LOG.warn("Route {} skipped: {}", route.getUuid(), e.getMessage(), e);
        }
        report.failReceiver(receiver.getUuid());
        continue;
      }

      try {
        AlertReceiverInterface handler =
            receiversManager.get(AlertUtils.getJsonTypeName(receiver.getParams()));
        handler.sendNotification(customer, alert, receiver);
        atLeastOneSucceeded = true;
        resolveAlert(customer, receiver);

        if (!receiver.getParams().continueSend) {
          break;
        }
      } catch (Exception e) {
        if (report.failuresByReceiver(receiver.getUuid()) == 0) {
          LOG.error(e.getMessage());
        }
        report.failReceiver(receiver.getUuid());
        createOrUpdateAlert(customer, receiver, "Error sending notification: " + e);
      }
    }
    if (!atLeastOneSucceeded) {
      report.failAttempt();
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

  private void createOrUpdateAlert(Customer c, AlertReceiver receiver, String details) {
    AlertFilter filter =
        AlertFilter.builder()
            .customerUuid(c.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .build();
    Alert alert =
        alertService
            .listNotResolved(filter)
            .stream()
            .findFirst()
            .orElse(
                new Alert()
                    .setCustomerUUID(c.getUuid())
                    .setErrCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
                    .setType(KnownAlertTypes.Error));
    alert
        .setMessage(details)
        .setLabels(AlertDefinitionLabelsBuilder.create().appendTarget(receiver).getAlertLabels());
    alertService.save(alert);
    if (!receiver.getUuid().equals(DEFAULT_ALERT_RECEIVER_UUID)) {
      // Resolve default in case it's active
      resolveAlert(c, getDefaultReceiver(c.getUuid()));
    }
  }

  private void resolveAlert(Customer c, AlertReceiver receiver) {
    AlertFilter filter =
        AlertFilter.builder()
            .customerUuid(c.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .label(KnownAlertLabels.TARGET_UUID, receiver.getUuid().toString())
            .build();
    alertService.markResolved(filter);
    if (!receiver.getUuid().equals(DEFAULT_ALERT_RECEIVER_UUID)) {
      // Resolve default in case it's active
      resolveAlert(c, getDefaultReceiver(c.getUuid()));
    }
  }
}
