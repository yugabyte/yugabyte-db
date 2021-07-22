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

import static com.yugabyte.yw.models.helpers.CommonUtils.nowPlusWithoutMillis;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertLabelsBuilder;
import com.yugabyte.yw.common.alerts.AlertNotificationReport;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverInterface;
import com.yugabyte.yw.common.alerts.AlertReceiverManager;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.common.alerts.YWValidateException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class AlertManager {

  private final EmailHelper emailHelper;
  private final AlertDefinitionGroupService alertDefinitionGroupService;
  private final AlertRouteService alertRouteService;
  private final AlertReceiverManager receiversManager;
  private final MetricService metricService;

  @Inject
  public AlertManager(
      EmailHelper emailHelper,
      AlertDefinitionGroupService alertDefinitionGroupService,
      AlertRouteService alertRouteService,
      AlertReceiverManager receiversManager,
      MetricService metricService) {
    this.emailHelper = emailHelper;
    this.alertDefinitionGroupService = alertDefinitionGroupService;
    this.alertRouteService = alertRouteService;
    this.receiversManager = receiversManager;
    this.metricService = metricService;
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
    AlertRoute route = alertRouteService.get(group.getCustomerUUID(), group.getRouteUUID());
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
      // Getting receivers from the default route.
      AlertRoute defaultRoute = alertRouteService.getDefaultRoute(alert.getCustomerUUID());
      if (defaultRoute == null) {
        log.warn(
            "Unable to notify about alert {}, there is no default route specified.",
            alert.getUuid());
        metricService.setStatusMetric(
            metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, customer),
            "Unable to notify about alert(s), there is no default route specified.");
        return;
      }

      List<AlertReceiver> defaultReceivers = defaultRoute.getReceiversList();
      if ((defaultReceivers.size() == 1)
          && ("Email".equals(AlertUtils.getJsonTypeName(defaultReceivers.get(0).getParams())))
          && ((AlertReceiverEmailParams) defaultReceivers.get(0).getParams()).defaultRecipients
          && CollectionUtils.isEmpty(emailHelper.getDestinations(customer.getUuid()))) {

        metricService.setStatusMetric(
            metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, customer),
            "Unable to notify about alert(s) using default route, "
                + "there are no recipients configured in the customer's profile.");
        return;
      }

      log.debug("For alert {} no routes/receivers found, using default route.", alert.getUuid());
      receivers.addAll(defaultReceivers);

      metricService.setOkStatusMetric(
          metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, customer));
    }

    for (AlertReceiver receiver : receivers) {
      try {
        AlertUtils.validate(receiver);
      } catch (YWValidateException e) {
        if (report.failuresByReceiver(receiver.getUuid()) == 0) {
          log.warn("Receiver {} skipped: {}", receiver.getUuid(), e.getMessage(), e);
        }
        report.failReceiver(receiver.getUuid());
        setReceiverStatusMetric(
            PlatformMetrics.ALERT_MANAGER_STATUS,
            receiver,
            "Misconfigured alert receiver: " + e.getMessage());
        continue;
      }

      try {
        AlertReceiverInterface handler =
            receiversManager.get(AlertUtils.getJsonTypeName(receiver.getParams()));
        handler.sendNotification(customer, alert, receiver);
        atLeastOneSucceeded = true;
        setOkReceiverStatusMetric(PlatformMetrics.ALERT_MANAGER_RECEIVER_STATUS, receiver);
      } catch (Exception e) {
        if (report.failuresByReceiver(receiver.getUuid()) == 0) {
          log.error(e.getMessage());
        }
        report.failReceiver(receiver.getUuid());
        setReceiverStatusMetric(
            PlatformMetrics.ALERT_MANAGER_RECEIVER_STATUS,
            receiver,
            "Error sending notification: " + e.getMessage());
      }
    }
    if (!atLeastOneSucceeded) {
      report.failAttempt();
    }
  }

  @VisibleForTesting
  void setOkReceiverStatusMetric(PlatformMetrics metric, AlertReceiver receiver) {
    setReceiverStatusMetric(metric, receiver, StringUtils.EMPTY);
  }

  @VisibleForTesting
  void setReceiverStatusMetric(PlatformMetrics metric, AlertReceiver receiver, String message) {
    boolean isSuccess = StringUtils.isEmpty(message);
    Metric statusMetric = buildMetricTemplate(metric, receiver).setValue(isSuccess ? 1.0 : 0.0);
    if (!isSuccess) {
      statusMetric.setLabel(KnownAlertLabels.ERROR_MESSAGE, message);
    }
    metricService.cleanAndSave(Collections.singletonList(statusMetric));
  }

  private Metric buildMetricTemplate(PlatformMetrics metric, AlertReceiver receiver) {
    return new Metric()
        .setExpireTime(
            nowPlusWithoutMillis(MetricService.DEFAULT_METRIC_EXPIRY_SEC, ChronoUnit.SECONDS))
        .setCustomerUUID(receiver.getCustomerUUID())
        .setType(Metric.Type.GAUGE)
        .setName(metric.getMetricName())
        .setTargetUuid(receiver.getUuid())
        .setLabels(AlertLabelsBuilder.create().appendTarget(receiver).getMetricLabels());
  }
}
