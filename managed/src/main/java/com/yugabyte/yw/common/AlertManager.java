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
import com.yugabyte.yw.common.alerts.AlertReceiverService;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.common.alerts.YWValidateException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
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

  @VisibleForTesting static final int NOTIFICATION_REPEAT_AFTER_FAILURE_IN_SECS = 180;

  private final EmailHelper emailHelper;
  private final AlertDefinitionGroupService alertDefinitionGroupService;
  private final AlertReceiverService alertReceiverService;
  private final AlertRouteService alertRouteService;
  private final AlertReceiverManager receiversManager;
  private final AlertService alertService;
  private final MetricService metricService;

  @Inject
  public AlertManager(
      EmailHelper emailHelper,
      AlertService alertService,
      AlertDefinitionGroupService alertDefinitionGroupService,
      AlertReceiverService alertReceiverService,
      AlertRouteService alertRouteService,
      AlertReceiverManager receiversManager,
      MetricService metricService) {
    this.emailHelper = emailHelper;
    this.alertService = alertService;
    this.alertDefinitionGroupService = alertDefinitionGroupService;
    this.alertReceiverService = alertReceiverService;
    this.alertRouteService = alertRouteService;
    this.receiversManager = receiversManager;
    this.metricService = metricService;
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

  @VisibleForTesting
  boolean sendNotificationForState(Alert alert, State state, AlertNotificationReport report) {
    boolean result = false;
    try {
      result = sendNotification(alert, state, report);

      alert.setNotificationAttemptTime(new Date());
      if (!result) {
        alert.setNotificationsFailed(alert.getNotificationsFailed() + 1);
        // For now using fixed delay before the notification repeat. Later the behavior
        // can be adjusted using an amount of failed attempts (using progressive value).
        alert.setNextNotificationTime(
            nowPlusWithoutMillis(NOTIFICATION_REPEAT_AFTER_FAILURE_IN_SECS, ChronoUnit.SECONDS));
        log.trace(
            "Next time to send notification for alert {} is {}",
            alert.getUuid(),
            alert.getNextNotificationTime());

        report.failAttempt();
      } else {
        // TODO: No repeats for now. Later should be updated along with the according
        // parameter introduced in AlertRoute.
        alert.setNextNotificationTime(null);
        alert.setNotificationsFailed(0);
        alert.setNotifiedState(state);
        log.trace("Notification sent for alert {}", alert.getUuid());
      }
      alert.save();

    } catch (Exception e) {
      report.failAttempt();
      log.error("Error while sending notification for alert {}", alert.getUuid(), e);
    }
    return result;
  }

  public void sendNotifications() {
    AlertFilter filter =
        AlertFilter.builder()
            .state(Alert.State.ACTIVE, Alert.State.RESOLVED)
            .notificationPending(true)
            .build();
    List<Alert> toNotify = alertService.list(filter);
    if (toNotify.size() == 0) {
      return;
    }

    log.debug("Sending notifications, {} alerts to proceed.", toNotify.size());
    AlertNotificationReport report = new AlertNotificationReport();
    for (Alert alert : toNotify) {
      try {
        if (((alert.getNotifiedState() == null)
                || (alert.getNotifiedState().ordinal() < State.ACTIVE.ordinal()))
            && (alert.getState().ordinal() >= State.ACTIVE.ordinal())) {
          report.raiseAttempt();
          if (!sendNotificationForState(alert, State.ACTIVE, report)) {
            continue;
          }
        }

        if ((alert.getNotifiedState().ordinal() < State.RESOLVED.ordinal())
            && (alert.getState() == State.RESOLVED)) {
          report.resolveAttempt();
          sendNotificationForState(alert, State.RESOLVED, report);
        }

      } catch (Exception e) {
        report.failAttempt();
        log.error("Error while sending notification for alert {}", alert.getUuid(), e);
      }
    }
    if (!report.isEmpty()) {
      log.info("{}", report);
    }
  }

  private boolean sendNotification(
      Alert alert, State stateToNotify, AlertNotificationReport report) {
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
        return false;
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
        return false;
      }

      log.debug("For alert {} no routes/receivers found, using default route.", alert.getUuid());
      receivers.addAll(defaultReceivers);

      metricService.setOkStatusMetric(
          metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, customer));
    }

    // Not going to save the alert, only to use with another state for the
    // notification.
    Alert tempAlert = alertService.get(alert.getUuid());
    tempAlert.setState(stateToNotify);

    for (AlertReceiver receiver : receivers) {
      try {
        alertReceiverService.validate(receiver);
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
        handler.sendNotification(customer, tempAlert, receiver);
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

    return atLeastOneSucceeded;
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
