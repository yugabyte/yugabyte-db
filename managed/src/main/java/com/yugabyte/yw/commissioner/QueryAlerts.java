/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Lists;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertNotificationReport;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.data.AlertData;
import com.yugabyte.yw.metrics.data.AlertState;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.jsonwebtoken.lang.Collections;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
@Slf4j
public class QueryAlerts {

  private static final int YB_QUERY_ALERTS_INTERVAL_SEC = 30;
  private static final int ALERTS_BATCH = 1000;
  private static final String SUMMARY_ANNOTATION_NAME = "summary";

  private AtomicBoolean running = new AtomicBoolean(false);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final MetricQueryHelper queryHelper;

  private final MetricService metricService;

  private final AlertService alertService;

  private final AlertDefinitionService alertDefinitionService;

  private final AlertDefinitionGroupService alertDefinitionGroupService;

  private final AlertManager alertManager;

  @Inject
  public QueryAlerts(
      ExecutionContext executionContext,
      ActorSystem actorSystem,
      AlertService alertService,
      MetricQueryHelper queryHelper,
      MetricService metricService,
      AlertDefinitionService alertDefinitionService,
      AlertDefinitionGroupService alertDefinitionGroupService,
      AlertManager alertManager) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.queryHelper = queryHelper;
    this.alertService = alertService;
    this.metricService = metricService;
    this.alertDefinitionService = alertDefinitionService;
    this.alertDefinitionGroupService = alertDefinitionGroupService;
    this.alertManager = alertManager;
    this.initialize();
  }

  private void initialize() {
    this.actorSystem
        .scheduler()
        .schedule(
            // Start 30 seconds later to allow Prometheus to get Platform metrics
            // and evaluate alerts based on them.
            Duration.create(YB_QUERY_ALERTS_INTERVAL_SEC, TimeUnit.SECONDS),
            Duration.create(YB_QUERY_ALERTS_INTERVAL_SEC, TimeUnit.SECONDS),
            this::scheduleRunner,
            this.executionContext);
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping querying for alerts for follower platform");
      return;
    }
    if (running.compareAndSet(false, true)) {
      try {
        try {
          List<UUID> activeAlertsUuids = processActiveAlerts();
          resolveAlerts(activeAlertsUuids);
          metricService.setOkStatusMetric(
              metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_STATUS));
        } catch (Exception e) {
          metricService.setStatusMetric(
              metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_STATUS),
              "Error querying for alerts: " + e.getMessage());
          log.error("Error querying for alerts alerts", e);
        }
        transitionAlerts();
      } finally {
        running.set(false);
      }
    }
  }

  private List<UUID> processActiveAlerts() {
    List<AlertData> alerts = queryHelper.queryAlerts();
    metricService.setMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS), alerts.size());
    List<AlertData> validAlerts =
        alerts
            .stream()
            .filter(alertData -> getCustomerUuid(alertData) != null)
            .filter(alertData -> getGroupUuid(alertData) != null)
            .filter(alertData -> getDefinitionUuid(alertData) != null)
            .collect(Collectors.toList());
    if (alerts.size() > validAlerts.size()) {
      log.warn(
          "Found {} alerts without customer, group or definition uuid",
          alerts.size() - validAlerts.size());
    }
    metricService.setMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_INVALID_ALERTS),
        alerts.size() - validAlerts.size());

    List<AlertData> activeAlerts =
        validAlerts
            .stream()
            .filter(alertData -> alertData.getState() != AlertState.pending)
            .collect(Collectors.toList());
    metricService.setMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_PENDING_ALERTS),
        validAlerts.size() - activeAlerts.size());

    List<AlertData> deduplicatedAlerts =
        new ArrayList<>(
            activeAlerts
                .stream()
                .collect(
                    Collectors.toMap(
                        this::getAlertKey,
                        Function.identity(),
                        (a, b) ->
                            getSeverity(a).getPriority() > getSeverity(b).getPriority() ? a : b,
                        LinkedHashMap::new))
                .values());

    List<UUID> activeAlertUuids = new ArrayList<>();
    for (List<AlertData> batch : Lists.partition(deduplicatedAlerts, ALERTS_BATCH)) {
      Set<UUID> definitionUuids =
          batch
              .stream()
              .map(this::getDefinitionUuid)
              .map(UUID::fromString)
              .collect(Collectors.toSet());

      AlertFilter alertFilter =
          AlertFilter.builder()
              .definitionUuids(definitionUuids)
              .targetState(Alert.State.ACTIVE, Alert.State.ACKNOWLEDGED)
              .build();
      Map<AlertKey, Alert> existingAlertsByKey =
          alertService
              .list(alertFilter)
              .stream()
              .collect(Collectors.toMap(this::getAlertKey, Function.identity()));

      AlertDefinitionFilter definitionFilter =
          AlertDefinitionFilter.builder().uuids(definitionUuids).build();
      Map<UUID, AlertDefinition> existingDefinitionsByUuid =
          alertDefinitionService
              .list(definitionFilter)
              .stream()
              .collect(Collectors.toMap(AlertDefinition::getUuid, Function.identity()));

      Set<UUID> groupUuids =
          existingDefinitionsByUuid
              .values()
              .stream()
              .map(AlertDefinition::getGroupUUID)
              .collect(Collectors.toSet());
      AlertDefinitionGroupFilter groupFilter =
          AlertDefinitionGroupFilter.builder().uuids(groupUuids).build();
      Map<UUID, AlertDefinitionGroup> existingGroupsByUuid =
          alertDefinitionGroupService
              .list(groupFilter)
              .stream()
              .collect(Collectors.toMap(AlertDefinitionGroup::getUuid, Function.identity()));

      List<Alert> toSave =
          batch
              .stream()
              .map(
                  data ->
                      processAlert(
                          data,
                          existingAlertsByKey,
                          existingDefinitionsByUuid,
                          existingGroupsByUuid))
              .filter(Objects::nonNull)
              .collect(Collectors.toList());
      metricService.setMetric(
          metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_FILTERED_ALERTS),
          activeAlerts.size() - toSave.size());
      long newAlerts = toSave.stream().filter(Alert::isNew).count();
      long updatedAlerts = toSave.size() - newAlerts;

      List<Alert> savedAlerts = alertService.save(toSave);
      metricService.setMetric(
          metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_NEW_ALERTS), newAlerts);
      metricService.setMetric(
          metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_UPDATED_ALERTS),
          updatedAlerts);

      activeAlertUuids.addAll(
          savedAlerts.stream().map(Alert::getUuid).collect(Collectors.toList()));
    }
    return activeAlertUuids;
  }

  private void resolveAlerts(List<UUID> activeAlertsUuids) {
    AlertFilter toResolveFilter = AlertFilter.builder().excludeUuids(activeAlertsUuids).build();
    List<Alert> resolved = alertService.markResolved(toResolveFilter);
    if (!resolved.isEmpty()) {
      log.info("Resolved {} alerts", resolved.size());
    }
    metricService.setMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_QUERY_RESOLVED_ALERTS),
        resolved.size());
  }

  private void transitionAlerts() {
    try {
      AlertNotificationReport report = new AlertNotificationReport();
      AlertFilter toSendRaisedFilter =
          AlertFilter.builder()
              .state(Alert.State.CREATED)
              .targetState(Alert.State.ACTIVE, Alert.State.RESOLVED)
              .build();
      List<Alert> toSendRaisedAlerts = alertService.list(toSendRaisedFilter);
      toSendRaisedAlerts.forEach(alert -> alertManager.transitionAlert(alert, report));

      AlertFilter toSendResolvedFilter =
          AlertFilter.builder().state(Alert.State.ACTIVE).targetState(Alert.State.RESOLVED).build();
      List<Alert> toSendResolvedAlerts = alertService.list(toSendResolvedFilter);
      toSendResolvedAlerts.forEach(alert -> alertManager.transitionAlert(alert, report));
      if (!report.isEmpty()) {
        log.info("{}", report);
      }
    } catch (Exception e) {
      log.error("Error sending notifications for alerts", e);
    }
  }

  private String getCustomerUuid(AlertData alertData) {
    if (Collections.isEmpty(alertData.getLabels())) {
      return null;
    }
    return alertData.getLabels().get(KnownAlertLabels.CUSTOMER_UUID.labelName());
  }

  private String getDefinitionUuid(AlertData alertData) {
    if (Collections.isEmpty(alertData.getLabels())) {
      return null;
    }
    return alertData.getLabels().get(KnownAlertLabels.DEFINITION_UUID.labelName());
  }

  private String getGroupUuid(AlertData alertData) {
    if (Collections.isEmpty(alertData.getLabels())) {
      return null;
    }
    return alertData.getLabels().get(KnownAlertLabels.GROUP_UUID.labelName());
  }

  private String getTargetUuid(AlertData alertData) {
    if (Collections.isEmpty(alertData.getLabels())) {
      return null;
    }
    return alertData.getLabels().get(KnownAlertLabels.TARGET_UUID.labelName());
  }

  private AlertKey getAlertKey(AlertData alertData) {
    return new AlertKey(getDefinitionUuid(alertData), getTargetUuid(alertData));
  }

  private AlertKey getAlertKey(Alert alert) {
    return new AlertKey(
        alert.getDefinitionUuid().toString(), alert.getLabelValue(KnownAlertLabels.TARGET_UUID));
  }

  private AlertDefinitionGroup.Severity getSeverity(AlertData alertData) {
    if (Collections.isEmpty(alertData.getLabels())) {
      return AlertDefinitionGroup.Severity.SEVERE;
    }
    return Optional.ofNullable(alertData.getLabels().get(KnownAlertLabels.SEVERITY.labelName()))
        .map(AlertDefinitionGroup.Severity::valueOf)
        .orElse(AlertDefinitionGroup.Severity.SEVERE);
  }

  private AlertDefinitionGroup.TargetType getGroupType(AlertData alertData) {
    if (Collections.isEmpty(alertData.getLabels())) {
      return AlertDefinitionGroup.TargetType.UNIVERSE;
    }
    return Optional.ofNullable(alertData.getLabels().get(KnownAlertLabels.GROUP_TYPE.labelName()))
        .map(AlertDefinitionGroup.TargetType::valueOf)
        .orElse(AlertDefinitionGroup.TargetType.UNIVERSE);
  }

  private Alert processAlert(
      AlertData alertData,
      Map<AlertKey, Alert> existingAlertsByKey,
      Map<UUID, AlertDefinition> definitionsByUuid,
      Map<UUID, AlertDefinitionGroup> groupsByUuid) {
    AlertKey alertKey = getAlertKey(alertData);
    if (alertKey.getDefinitionUuid() == null) {
      // Should be filtered earlier
      log.error("Alert {} has no definition uuid", alertData);
      return null;
    }
    String groupUuidStr = getGroupUuid(alertData);
    if (groupUuidStr == null) {
      // Should be filtered earlier
      log.error("Alert {} has no group uuid", alertData);
      return null;
    }
    if (alertData.getState() == AlertState.pending) {
      // Should be filtered earlier
      log.error("Alert {} is in pending state - skip for now", alertData);
      return null;
    }
    UUID definitionUuid = UUID.fromString(alertKey.getDefinitionUuid());
    AlertDefinition definition = definitionsByUuid.get(definitionUuid);
    if (definition == null) {
      log.debug("Definition is missing for alert {}", alertData);
      return null;
    }
    UUID groupUuid = UUID.fromString(groupUuidStr);
    AlertDefinitionGroup group = groupsByUuid.get(groupUuid);
    if (group == null || !group.isActive()) {
      log.debug("Definition group is missing or inactive for alert {}", alertData);
      return null;
    }
    Alert alert = existingAlertsByKey.get(alertKey);
    if (alert == null) {
      String customerUuid = alertData.getLabels().get(KnownAlertLabels.CUSTOMER_UUID.labelName());
      if (StringUtils.isEmpty(customerUuid)) {
        log.debug("Alert {} has no customer UUID", alertData);
        return null;
      }

      alert =
          new Alert()
              .setCreateTime(Date.from(alertData.getActiveAt().toInstant()))
              .setCustomerUUID(UUID.fromString(customerUuid))
              .setDefinitionUuid(definitionUuid)
              .setGroupUuid(groupUuid);
    }
    AlertDefinitionGroup.Severity severity = getSeverity(alertData);
    AlertDefinitionGroup.TargetType groupType = getGroupType(alertData);
    String message = alertData.getAnnotations().get(SUMMARY_ANNOTATION_NAME);

    List<AlertLabel> labels =
        alertData
            .getLabels()
            .entrySet()
            .stream()
            .map(e -> new AlertLabel(e.getKey(), e.getValue()))
            .sorted(Comparator.comparing(AlertLabel::getName))
            .collect(Collectors.toList());
    alert.setSeverity(severity).setGroupType(groupType).setMessage(message).setLabels(labels);
    return alert;
  }

  @Value
  private static class AlertKey {
    String definitionUuid;
    String targetUuid;
  }
}
