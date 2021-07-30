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
  // Health check
  HEALTH_CHECK_STATUS("Health check status for universe", Unit.STATUS),
  HEALTH_CHECK_NODES_WITH_ERRORS("Nodes with at least 1 error count", Unit.COUNT),
  HEALTH_CHECK_MASTER_DOWN("Master process down nodes count", Unit.COUNT),
  HEALTH_CHECK_MASTER_FATAL_LOGS("Master fatal log nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_DOWN("TServer process down nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_FATAL_LOGS("TServer fatal log nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_CORE_FILES("TServer core files nodes count", Unit.COUNT),
  HEALTH_CHECK_YSQLSH_CONNECTIVITY_ERROR("Ysqlsh connectivity error nodes count", Unit.COUNT),
  HEALTH_CHECK_CQLSH_CONNECTIVITY_ERROR("Cqlsh connectivity error nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_DISK_UTILIZATION_HIGH(
      "TServer high disk utilization nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_OPENED_FD_HIGH(
      "TServer high number of opened file descriptors nodes count", Unit.COUNT),
  HEALTH_CHECK_TSERVER_CLOCK_SYNCHRONIZATION_ERROR(
      "TServer clock synchronization error nodes count", Unit.COUNT),
  HEALTH_CHECK_NODE_METRICS_STATUS("Health check node metrics status for universe", Unit.STATUS),
  HEALTH_CHECK_NOTIFICATION_STATUS("Health check notification status for universe", Unit.STATUS),
  // Tasks
  CREATE_BACKUP_STATUS("Backup creation task status for universe", Unit.STATUS),
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
  ALERT_CONFIG_SYNC_FAILED("Number of config sync failures", Unit.COUNT),
  ALERT_CONFIG_WRITTEN("Alert rule files written", Unit.COUNT),
  ALERT_CONFIG_REMOVED("Alert rule files removed", Unit.COUNT),
  ALERT_MANAGER_STATUS("Common alert manager status for customer", Unit.STATUS),
  ALERT_MANAGER_RECEIVER_STATUS("Alert manager receiver status", Unit.STATUS);

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
