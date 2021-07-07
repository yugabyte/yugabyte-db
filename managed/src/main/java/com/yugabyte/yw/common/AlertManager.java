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

import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;

import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.*;
import org.apache.commons.collections.CollectionUtils;

@Singleton
@Slf4j
public class AlertManager {

  private final EmailHelper emailHelper;
  private final AlertService alertService;
  private final AlertDefinitionGroupService alertDefinitionGroupService;
  private final AlertReceiverManager receiversManager;

  @Inject
  public AlertManager(
      EmailHelper emailHelper,
      AlertService alertService,
      AlertDefinitionGroupService alertDefinitionGroupService,
      AlertReceiverManager receiversManager) {
    this.emailHelper = emailHelper;
    this.alertService = alertService;
    this.alertDefinitionGroupService = alertDefinitionGroupService;
    this.receiversManager = receiversManager;
  }

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
          log.info("Transitioning alert {} to active", alert.getUuid());
          report.raiseAttempt();
          alert.setState(Alert.State.ACTIVE);
          sendNotification(alert, report);
          break;
        case ACTIVE:
          log.info("Transitioning alert {} to resolved (with email)", alert.getUuid());
          report.resolveAttempt();
          alert.setState(Alert.State.RESOLVED);
          sendNotification(alert, report);
          break;
        default:
          log.warn(
              "Unexpected alert state {} during notification for alert {}",
              alert.getState().name(),
              alert.getUuid());
      }

      alert.save();
    } catch (Exception e) {
      report.failAttempt();
      log.error("Error transitioning alert state for alert {}", alert.getUuid(), e);
    }

    return alert;
  }

  private Optional<AlertRoute> getRouteByAlert(Alert alert) {
    String groupUuid = alert.getLabelValue(KnownAlertLabels.GROUP_UUID);
    if (groupUuid == null) {
      return Optional.empty();
    }
    AlertDefinitionGroup group = alertDefinitionGroupService.get(UUID.fromString(groupUuid));
    if (group == null) {
      log.warn("Missing group {} for alert {}", groupUuid, alert.getUuid());
      return Optional.empty();
    }
    if (group.getRouteUUID() == null) {
      return Optional.empty();
    }
    AlertRoute route = AlertRoute.get(group.getCustomerUUID(), group.getRouteUUID());
    if (route == null) {
      log.warn("Missing route {} for alert {}", group.getRouteUUID(), alert.getUuid());
      return Optional.empty();
    }
    return Optional.of(route);
  }

  public void sendNotification(Alert alert, AlertNotificationReport report) {
    Customer customer = Customer.get(alert.getCustomerUUID());

    boolean atLeastOneSucceeded = false;
    Optional<AlertRoute> route = getRouteByAlert(alert);
    List<AlertReceiver> receivers =
        new ArrayList<>(route.map(AlertRoute::getReceiversList).orElse(Collections.emptyList()));

    if (receivers.isEmpty()) {
      if (!alert.isSendEmail()) {
        return;
      }

      // Getting receivers from the default route.
      AlertRoute defaultRoute = AlertRoute.getDefaultRoute(alert.getCustomerUUID());
      if (defaultRoute == null) {
        log.warn(
            "Unable to notify about alert {}, there is no default route specified.",
            alert.getUuid());
        createOrUpdateAlertForCustomer(
            customer,
            "Unable to notify about alert(s), there is no default route specified.",
            KnownAlertTypes.Error);
        return;
      }
      resolveAlert(customer, customer.getUuid());

      List<AlertReceiver> defaultReceivers = defaultRoute.getReceiversList();
      if ((defaultReceivers.size() == 1)
          && ("Email".equals(AlertUtils.getJsonTypeName(defaultReceivers.get(0).getParams())))
          && ((AlertReceiverEmailParams) defaultReceivers.get(0).getParams()).defaultRecipients
          && CollectionUtils.isEmpty(emailHelper.getDestinations(customer.getUuid()))) {
        createOrUpdateAlertForCustomer(
            customer,
            "Unable to notify about alert(s) using default route, "
                + "there are no recipients configured in the customer's profile.",
            KnownAlertTypes.Warning);
        return;
      }

      log.debug("For alert {} no routes/receivers found, using default route.", alert.getUuid());
      receivers.addAll(defaultReceivers);
    }

    for (AlertReceiver receiver : receivers) {
      try {
        AlertUtils.validate(receiver);
      } catch (YWValidateException e) {
        if (report.failuresByReceiver(receiver.getUuid()) == 0) {
          log.warn("Receiver {} skipped: {}", receiver.getUuid(), e.getMessage(), e);
        }
        report.failReceiver(receiver.getUuid());
        createOrUpdateAlertForReceiver(
            customer, receiver, "Misconfigured alert receiver: " + e, KnownAlertTypes.Error);
        continue;
      }

      try {
        AlertReceiverInterface handler =
            receiversManager.get(AlertUtils.getJsonTypeName(receiver.getParams()));
        handler.sendNotification(customer, alert, receiver);
        atLeastOneSucceeded = true;
        resolveAlert(customer, receiver.getUuid());
      } catch (Exception e) {
        if (report.failuresByReceiver(receiver.getUuid()) == 0) {
          log.error(e.getMessage());
        }
        report.failReceiver(receiver.getUuid());
        createOrUpdateAlertForReceiver(
            customer, receiver, "Error sending notification: " + e, KnownAlertTypes.Error);
      }
    }
    if (!atLeastOneSucceeded) {
      report.failAttempt();
    }
  }

  private void createOrUpdateAlertForReceiver(
      Customer c, AlertReceiver receiver, String details, KnownAlertTypes errorType) {
    createOrUpdateAlert(
        c,
        receiver.getUuid(),
        details,
        AlertDefinitionLabelsBuilder.create().appendTarget(receiver).getAlertLabels(),
        errorType);
  }

  private void createOrUpdateAlertForCustomer(
      Customer c, String details, KnownAlertTypes errorType) {
    createOrUpdateAlert(
        c,
        c.getUuid(),
        details,
        AlertDefinitionLabelsBuilder.create().appendTarget(c).getAlertLabels(),
        errorType);
  }

  private void createOrUpdateAlert(
      Customer c,
      UUID targetUUID,
      String details,
      List<AlertLabel> labels,
      KnownAlertTypes errorType) {
    AlertFilter filter =
        AlertFilter.builder()
            .customerUuid(c.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .label(KnownAlertLabels.TARGET_UUID, targetUUID.toString())
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
                    .setType(errorType));
    alert.setMessage(details).setLabels(labels);
    alertService.save(alert);
  }

  private void resolveAlert(Customer c, UUID targetUUID) {
    AlertFilter filter =
        AlertFilter.builder()
            .customerUuid(c.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .label(KnownAlertLabels.TARGET_UUID, targetUUID.toString())
            .build();
    alertService.markResolved(filter);
  }
}
