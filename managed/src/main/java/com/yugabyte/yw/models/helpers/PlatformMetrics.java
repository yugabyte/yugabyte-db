/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.models.common.Unit;
import java.util.Arrays;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Getter
@Slf4j
public enum PlatformMetrics {
  // Health check common
  HEALTH_CHECK_STATUS("Health check status for universe", Unit.STATUS),
  HEALTH_CHECK_NODES_WITH_ERRORS("Nodes with at least 1 error count", Unit.COUNT),
  HEALTH_CHECK_NODE_METRICS_STATUS("Health check node metrics status for universe", Unit.STATUS),
  HEALTH_CHECK_NOTIFICATION_STATUS("Health check notification status for universe", Unit.STATUS),
  YB_UNIV_HEALTH_STATUS("Particular node check status", Unit.STATUS, true, "yb_univ_health_status"),

  // Health check error nodes count
  HEALTH_CHECK_MASTER_DOWN("Master process down nodes count", Unit.COUNT),
  HEALTH_CHECK_MASTER_VERSION_MISMATCH("Master nodes with version mismatch count", Unit.COUNT),
  HEALTH_CHECK_MASTER_ERROR_LOGS("Master error log nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_DOWN("TServer process down nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_VERSION_MISMATCH("TServer nodes with version mismatch count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_ERROR_LOGS("TServer error log nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_CORE_FILES("TServer core files nodes count", Unit.COUNT),
  HEALTH_CHECK_YSQLSH_CONNECTIVITY_ERROR("Ysqlsh connectivity error nodes count", Unit.COUNT),
  HEALTH_CHECK_CQLSH_CONNECTIVITY_ERROR("Cqlsh connectivity error nodes count", Unit.COUNT),
  HEALTH_CHECK_REDIS_CONNECTIVITY_ERROR("Redis connectivity error nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_DISK_UTILIZATION_HIGH(
      "TServer high disk utilization nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_OPENED_FD_HIGH(
      "TServer high number of opened file descriptors nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_CLOCK_SYNCHRONIZATION_ERROR(
      "TServer clock synchronization error nodes count", Unit.COUNT),
  HEALTH_CHECK_N2N_CA_CERT("TServer expired Node to Node CA certificate nodes count", Unit.COUNT),
  HEALTH_CHECK_N2N_CERT("TServer expired Node to Node certificate nodes count", Unit.COUNT),
  HEALTH_CHECK_C2N_CA_CERT("TServer expired Client to Node CA certificate nodes count", Unit.COUNT),
  HEALTH_CHECK_C2N_CERT("TServer expired Client to Node certificate nodes count", Unit.COUNT),
  HEALTH_CHECK_CLIENT_CA_CERT("TServer expired Client CA certificate nodes count", Unit.COUNT),
  HEALTH_CHECK_CLIENT_CERT("TServer expired Client certificate nodes count", Unit.COUNT),
  HEALTH_CHECK_YB_CONTROLLER_DOWN("YB-Controller process down nodes count", Unit.COUNT),

  // Tasks
  CREATE_BACKUP_STATUS("Backup creation task status for universe", Unit.STATUS),
  SCHEDULE_BACKUP_STATUS("Backup schedule status for universe", Unit.STATUS),
  @Deprecated
  UNIVERSE_INACTIVE_CRON_NODES("Count of nodes with inactive cronjob for universe", Unit.COUNT),
  SSH_KEY_ROTATION_STATUS("SSH Key rotation task status for universe", Unit.STATUS, false),
  PITR_CONFIG_STATUS("PITR Config Status for DB", Unit.STATUS),
  // Alert Subsystem
  ALERT_QUERY_STATUS("Alert query status", Unit.STATUS),
  ALERT_QUERY_TOTAL_ALERTS("Total number of alerts, returned by Prometheus", Unit.COUNT),
  ALERT_QUERY_INVALID_ALERTS("Number of invalid alerts, returned by Prometheus", Unit.COUNT),
  ALERT_QUERY_PENDING_ALERTS("Number of pending alerts, returned by Prometheus", Unit.COUNT),
  ALERT_QUERY_FILTERED_ALERTS(
      "Number of alerts, returned by Prometheus, which were filtered"
          + " during processing (same alert with both severities, missing customer,"
          + " group or definition, etc.)",
      Unit.COUNT),
  ALERT_QUERY_NEW_ALERTS("Number of raised alerts", Unit.COUNT),
  ALERT_QUERY_UPDATED_ALERTS("Number of updated active alerts", Unit.COUNT),
  ALERT_QUERY_RESOLVED_ALERTS("Number of resolved alerts", Unit.COUNT),
  ALERT_CONFIG_WRITER_STATUS("Alerting rules configuration writer status", Unit.STATUS),
  ALERT_MAINTENANCE_WINDOW_PROCESSOR_STATUS(
      "Maintenance windows alert processor status", Unit.STATUS),
  ALERT_CONFIG_SYNC_FAILED("Number of config sync failures", Unit.COUNT),
  ALERT_CONFIG_WRITTEN("Alert rule files written", Unit.COUNT),
  ALERT_CONFIG_REMOVED("Alert rule files removed", Unit.COUNT),
  ALERT_MANAGER_STATUS("Common alert manager status for customer", Unit.STATUS),
  ALERT_MANAGER_CHANNEL_STATUS("Alert manager channel status", Unit.STATUS),
  METRIC_PROCESSOR_STATUS("Platform metrics processor status", Unit.STATUS),
  SWAMPER_FILE_UPDATER_STATUS("Swamper target updater status", Unit.STATUS),

  UNIVERSE_EXISTS("Flag, indicating that universe exists", Unit.STATUS, false),
  UNIVERSE_PAUSED("Flag, indicating that universe is paused", Unit.STATUS, false),
  UNIVERSE_UPDATE_IN_PROGRESS(
      "Flag, indicating that universe update is in progress", Unit.STATUS, false),
  UNIVERSE_ACTIVE_TASK_CODE(
      "Code of the active task, running on the universe. If no task is running - value is 0",
      Unit.STATUS,
      false),
  UNIVERSE_BACKUP_IN_PROGRESS(
      "Flag, indicating that universe backup is in progress", Unit.STATUS, false),
  UNIVERSE_NODE_FUNCTION("Flag, indicating expected node functions", Unit.STATUS, false),
  UNIVERSE_NODE_PROCESS_STATUS("Flag, indicating expected node process status", Unit.STATUS, false),
  UNIVERSE_ENCRYPTION_KEY_EXPIRY_DAY(
      "Remaining Encryption-at-Rest config validity in days", Unit.DAY, false),
  UNIVERSE_REPLICATION_FACTOR("Universe replication factor", Unit.COUNT, true),
  UNIVERSE_SSH_KEY_EXPIRY_DAY(
      "Remaining days to expiry for SSH key of the universe", Unit.DAY, false),
  UNIVERSE_METRIC_COLLECTION_STATUS("Metric Collection status for the universe", Unit.STATUS),
  UNIVERSE_PRIVATE_ACCESS_KEY_STATUS(
      "Flag, indicating that private access key's permission is unchanged", Unit.STATUS),
  UNIVERSE_NODE_PROVISIONED_IOPS("Provisioned IOPS for the node disk", Unit.COUNT, false),
  UNIVERSE_NODE_PROVISIONED_THROUGHPUT(
      "Provisioned throughput for the node disk", Unit.COUNT, false),
  // Used for k8s universes only.
  CONTAINER_RESOURCE_REQUESTS_CPU_CORES(
      "Requested number of CPU cores for the db process", Unit.COUNT, false),
  UNIVERSE_NODE_CRON_STATUS("Cron jobs status on the node", Unit.STATUS),
  UNIVERSE_IS_SYSTEMD("Flag, indicating systemd universe", Unit.STATUS, false),
  UNIVERSE_OS_UPDATE_REQUIRED(
      "More recent OS version is recommended for this universe", Unit.STATUS, false);
  private final String help;
  private final Unit unit;
  private final Set<MetricSourceState> validForSourceStates;
  private final String metricName;

  // By default metrics are valid only for active source
  PlatformMetrics(String help, Unit unit) {
    this(help, unit, true);
  }

  PlatformMetrics(String help, Unit unit, boolean onlyActive) {
    this(help, unit, onlyActive, null);
  }

  PlatformMetrics(String help, Unit unit, boolean onlyActive, String metricName) {
    Set<MetricSourceState> validForSourceStates =
        onlyActive
            ? ImmutableSet.of(MetricSourceState.ACTIVE)
            : ImmutableSet.of(MetricSourceState.ACTIVE, MetricSourceState.INACTIVE);
    this.help = help;
    this.unit = unit;
    this.validForSourceStates = validForSourceStates;
    this.metricName = metricName;
  }

  public String getUnitName() {
    return unit != null ? unit.getMetricName() : StringUtils.EMPTY;
  }

  public String getMetricName() {
    if (metricName != null) {
      return metricName;
    }
    // ybp is required to list all platform alerts in Prometheus UI by prefix
    return "ybp_" + name().toLowerCase();
  }

  public static Set<PlatformMetrics> invalidForState(MetricSourceState state) {
    return Arrays.stream(values())
        .filter(m -> !m.getValidForSourceStates().contains(state))
        .collect(Collectors.toSet());
  }

  public static PlatformMetrics fromMetricName(String metricName) {
    try {
      return valueOf(metricName.substring(4).toUpperCase());
    } catch (IllegalArgumentException iae) {
      // This is node metric from health check.
      log.trace("Enum value not found for metric {}", metricName);
      return null;
    }
  }
}
