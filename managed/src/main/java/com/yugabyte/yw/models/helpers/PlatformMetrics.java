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

import com.yugabyte.yw.models.common.Unit;
import org.apache.commons.lang3.StringUtils;

public enum PlatformMetrics {
  // Health check common
  HEALTH_CHECK_STATUS("Health check status for universe", Unit.STATUS),
  HEALTH_CHECK_NODES_WITH_ERRORS("Nodes with at least 1 error count", Unit.COUNT),
  HEALTH_CHECK_NODE_METRICS_STATUS("Health check node metrics status for universe", Unit.STATUS),
  HEALTH_CHECK_NOTIFICATION_STATUS("Health check notification status for universe", Unit.STATUS),

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

  // Health check node metrics
  HEALTH_CHECK_MASTER_BOOT_TIME_SEC("Master process boot time in seconds from epoch", Unit.SECOND),
  HEALTH_CHECK_TSERVER_BOOT_TIME_SEC(
      "TServer process boot time in seconds from epoch", Unit.SECOND),
  HEALTH_CHECK_NODE_MASTER_FATAL_LOGS("Master process recent fatal logs", Unit.STATUS),
  HEALTH_CHECK_NODE_MASTER_ERROR_LOGS("Master process recent error logs", Unit.STATUS),
  HEALTH_CHECK_NODE_TSERVER_FATAL_LOGS("TServer process recent fatal logs", Unit.STATUS),
  HEALTH_CHECK_NODE_TSERVER_ERROR_LOGS("TServer process recent error logs", Unit.STATUS),
  HEALTH_CHECK_N2N_CA_CERT_VALIDITY_DAYS(
      "Remaining Node to Node CA certificate validity days", Unit.DAY),
  HEALTH_CHECK_N2N_CERT_VALIDITY_DAYS("Remaining Node to Node certificate validity days", Unit.DAY),
  HEALTH_CHECK_C2N_CA_CERT_VALIDITY_DAYS(
      "Remaining Client to Node CA certificate validity days", Unit.DAY),
  HEALTH_CHECK_C2N_CERT_VALIDITY_DAYS(
      "Remaining Client to Node certificate validity days", Unit.DAY),
  HEALTH_CHECK_USED_FD_PCT("Percentage of used on the node file descriptors ", Unit.PERCENT),

  // Tasks
  CREATE_BACKUP_STATUS("Backup creation task status for universe", Unit.STATUS),
  SCHEDULE_BACKUP_STATUS("Backup schedule status for universe", Unit.STATUS),
  UNIVERSE_INACTIVE_CRON_NODES("Count of nodes with inactive cronjob for universe", Unit.COUNT),
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

  UNIVERSE_EXISTS("Flag, indicating that universe exists", Unit.STATUS),
  UNIVERSE_PAUSED("Flag, indicating that universe is paused", Unit.STATUS),
  UNIVERSE_UPDATE_IN_PROGRESS("Flag, indicating that universe update is in progress", Unit.STATUS),
  UNIVERSE_BACKUP_IN_PROGRESS("Flag, indicating that universe backup is in progress", Unit.STATUS),
  UNIVERSE_NODE_FUNCTION("Flag, indicating expected node functions", Unit.STATUS);

  private final String help;
  private final Unit unit;

  PlatformMetrics(String help, Unit unit) {
    this.help = help;
    this.unit = unit;
  }

  public String getHelp() {
    return help;
  }

  public Unit getUnit() {
    return unit;
  }

  public String getUnitName() {
    return unit != null ? unit.getMetricName() : StringUtils.EMPTY;
  }

  public String getMetricName() {
    // ybp is required to list all platform alerts in Prometheus UI by prefix
    return "ybp_" + name().toLowerCase();
  }
}
