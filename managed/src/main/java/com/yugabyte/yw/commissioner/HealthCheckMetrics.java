/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Gauge;
import java.util.Collections;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class HealthCheckMetrics {
  public static final String kUnivMetricName = "yb_univ_health_status";
  public static final String kUnivUUIDLabel = "univ_uuid";
  public static final String kUnivNameLabel = "univ_name";
  public static final String kCheckLabel = "check_name";
  public static final String kNodeLabel = "node";

  public static final String UPTIME_CHECK = "Uptime";
  private static final String VERSION_MISMATCH_CHECK = "YB Version";
  private static final String FATAL_LOG_CHECK = "Fatal log files";
  private static final String CQLSH_CONNECTIVITY_CHECK = "Connectivity with cqlsh";
  private static final String YSQLSH_CONNECTIVITY_CHECK = "Connectivity with ysqlsh";
  private static final String REDIS_CONNECTIVITY_CHECK = "Connectivity with redis-cli";
  private static final String DISK_UTILIZATION_CHECK = "Disk utilization";
  private static final String CORE_FILES_CHECK = "Core files";
  private static final String OPENED_FILE_DESCRIPTORS_CHECK = "Opened file descriptors";
  private static final String CLOCK_SYNC_CHECK = "Clock synchronization";
  private static final String NODE_TO_NODE_CA_CERT_CHECK = "Node To Node CA Cert Expiry Days";
  private static final String NODE_TO_NODE_CERT_CHECK = "Node To Node Cert Expiry Days";
  private static final String CLIENT_TO_NODE_CA_CERT_CHECK = "Client To Node CA Cert Expiry Days";
  private static final String CLIENT_TO_NODE_CERT_CHECK = "Client To Node Cert Expiry Days";
  private static final String CLIENT_CA_CERT_CHECK = "Client CA Cert Expiry Days";
  private static final String CLIENT_CERT_CHECK = "Client Cert Expiry Days";

  public static final List<PlatformMetrics> HEALTH_CHECK_METRICS_WITHOUT_STATUS =
      ImmutableList.<PlatformMetrics>builder()
          .add(PlatformMetrics.HEALTH_CHECK_MASTER_DOWN)
          .add(PlatformMetrics.HEALTH_CHECK_MASTER_VERSION_MISMATCH)
          .add(PlatformMetrics.HEALTH_CHECK_MASTER_ERROR_LOGS)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_DOWN)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_ERROR_LOGS)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_CORE_FILES)
          .add(PlatformMetrics.HEALTH_CHECK_YSQLSH_CONNECTIVITY_ERROR)
          .add(PlatformMetrics.HEALTH_CHECK_CQLSH_CONNECTIVITY_ERROR)
          .add(PlatformMetrics.HEALTH_CHECK_REDIS_CONNECTIVITY_ERROR)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_DISK_UTILIZATION_HIGH)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_OPENED_FD_HIGH)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_CLOCK_SYNCHRONIZATION_ERROR)
          .add(PlatformMetrics.HEALTH_CHECK_N2N_CA_CERT)
          .add(PlatformMetrics.HEALTH_CHECK_N2N_CERT)
          .add(PlatformMetrics.HEALTH_CHECK_C2N_CA_CERT)
          .add(PlatformMetrics.HEALTH_CHECK_C2N_CERT)
          .add(PlatformMetrics.HEALTH_CHECK_MASTER_BOOT_TIME_SEC)
          .add(PlatformMetrics.HEALTH_CHECK_TSERVER_BOOT_TIME_SEC)
          .add(PlatformMetrics.HEALTH_CHECK_N2N_CA_CERT_VALIDITY_DAYS)
          .add(PlatformMetrics.HEALTH_CHECK_N2N_CERT_VALIDITY_DAYS)
          .add(PlatformMetrics.HEALTH_CHECK_C2N_CA_CERT_VALIDITY_DAYS)
          .add(PlatformMetrics.HEALTH_CHECK_C2N_CERT_VALIDITY_DAYS)
          .build();

  public static final List<PlatformMetrics> HEALTH_CHECK_METRICS =
      ImmutableList.<PlatformMetrics>builder()
          .add(PlatformMetrics.HEALTH_CHECK_STATUS)
          .addAll(HEALTH_CHECK_METRICS_WITHOUT_STATUS)
          .build();

  private Gauge healthMetric;

  @VisibleForTesting
  HealthCheckMetrics(CollectorRegistry registry) {
    this.initialize(registry);
  }

  @Inject
  public HealthCheckMetrics() {
    this(CollectorRegistry.defaultRegistry);
  }

  private void initialize(CollectorRegistry registry) {
    try {
      healthMetric =
          Gauge.build(kUnivMetricName, "Boolean result of health checks")
              .labelNames(kUnivUUIDLabel, kUnivNameLabel, kNodeLabel, kCheckLabel)
              .register(registry);
    } catch (IllegalArgumentException e) {
      log.warn("Failed to build prometheus gauge for name: " + kUnivMetricName);
    }
  }

  public Gauge getHealthMetric() {
    return healthMetric;
  }

  public static PlatformMetrics getCountMetricByCheckName(String checkName, boolean isMaster) {
    switch (checkName) {
      case UPTIME_CHECK:
        if (isMaster) {
          return PlatformMetrics.HEALTH_CHECK_MASTER_DOWN;
        } else {
          return PlatformMetrics.HEALTH_CHECK_TSERVER_DOWN;
        }
      case VERSION_MISMATCH_CHECK:
        if (isMaster) {
          return PlatformMetrics.HEALTH_CHECK_MASTER_VERSION_MISMATCH;
        } else {
          return PlatformMetrics.HEALTH_CHECK_TSERVER_VERSION_MISMATCH;
        }
      case FATAL_LOG_CHECK:
        if (isMaster) {
          return PlatformMetrics.HEALTH_CHECK_MASTER_ERROR_LOGS;
        } else {
          return PlatformMetrics.HEALTH_CHECK_TSERVER_ERROR_LOGS;
        }
      case CQLSH_CONNECTIVITY_CHECK:
        return PlatformMetrics.HEALTH_CHECK_CQLSH_CONNECTIVITY_ERROR;
      case YSQLSH_CONNECTIVITY_CHECK:
        return PlatformMetrics.HEALTH_CHECK_YSQLSH_CONNECTIVITY_ERROR;
      case REDIS_CONNECTIVITY_CHECK:
        return PlatformMetrics.HEALTH_CHECK_REDIS_CONNECTIVITY_ERROR;
      case DISK_UTILIZATION_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_DISK_UTILIZATION_HIGH;
      case CORE_FILES_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_CORE_FILES;
      case OPENED_FILE_DESCRIPTORS_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_OPENED_FD_HIGH;
      case CLOCK_SYNC_CHECK:
        return PlatformMetrics.HEALTH_CHECK_TSERVER_CLOCK_SYNCHRONIZATION_ERROR;
      case NODE_TO_NODE_CA_CERT_CHECK:
        return PlatformMetrics.HEALTH_CHECK_N2N_CA_CERT;
      case NODE_TO_NODE_CERT_CHECK:
        return PlatformMetrics.HEALTH_CHECK_N2N_CERT;
      case CLIENT_TO_NODE_CA_CERT_CHECK:
        return PlatformMetrics.HEALTH_CHECK_C2N_CA_CERT;
      case CLIENT_TO_NODE_CERT_CHECK:
        return PlatformMetrics.HEALTH_CHECK_C2N_CERT;
      case CLIENT_CA_CERT_CHECK:
        return PlatformMetrics.HEALTH_CHECK_CLIENT_CA_CERT;
      case CLIENT_CERT_CHECK:
        return PlatformMetrics.HEALTH_CHECK_CLIENT_CERT;
      default:
        return null;
    }
  }

  public static List<Metric> getNodeMetrics(
      String checkName, boolean isMaster, Universe universe, String nodeName, double metricValue) {
    switch (checkName) {
      case UPTIME_CHECK:
        if (isMaster) {
          return Collections.singletonList(
              buildNodeMetric(
                  PlatformMetrics.HEALTH_CHECK_MASTER_BOOT_TIME_SEC,
                  universe,
                  nodeName,
                  metricValue));
        } else {
          return Collections.singletonList(
              buildNodeMetric(
                  PlatformMetrics.HEALTH_CHECK_TSERVER_BOOT_TIME_SEC,
                  universe,
                  nodeName,
                  metricValue));
        }
      case FATAL_LOG_CHECK:
        // 1 or 3 == error logs exist == 0 status
        double errorLogsValue = (metricValue + 1) % 2;
        // 2 or 3 == fatal logs exist == 0 status
        double fatalLogsValue = metricValue < 2 ? 1 : 0;
        if (isMaster) {
          return ImmutableList.of(
              buildNodeMetric(
                  PlatformMetrics.HEALTH_CHECK_NODE_MASTER_FATAL_LOGS,
                  universe,
                  nodeName,
                  fatalLogsValue),
              buildNodeMetric(
                  PlatformMetrics.HEALTH_CHECK_NODE_MASTER_ERROR_LOGS,
                  universe,
                  nodeName,
                  errorLogsValue));
        } else {
          return ImmutableList.of(
              buildNodeMetric(
                  PlatformMetrics.HEALTH_CHECK_NODE_TSERVER_FATAL_LOGS,
                  universe,
                  nodeName,
                  fatalLogsValue),
              buildNodeMetric(
                  PlatformMetrics.HEALTH_CHECK_NODE_TSERVER_ERROR_LOGS,
                  universe,
                  nodeName,
                  errorLogsValue));
        }
      case OPENED_FILE_DESCRIPTORS_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_USED_FD_PCT, universe, nodeName, metricValue));
      case NODE_TO_NODE_CA_CERT_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_N2N_CA_CERT_VALIDITY_DAYS,
                universe,
                nodeName,
                metricValue));
      case NODE_TO_NODE_CERT_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_N2N_CERT_VALIDITY_DAYS,
                universe,
                nodeName,
                metricValue));
      case CLIENT_TO_NODE_CA_CERT_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_C2N_CA_CERT_VALIDITY_DAYS,
                universe,
                nodeName,
                metricValue));
      case CLIENT_TO_NODE_CERT_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_C2N_CERT_VALIDITY_DAYS,
                universe,
                nodeName,
                metricValue));
      case CLIENT_CA_CERT_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_CLIENT_CA_CERT_VALIDITY_DAYS,
                universe,
                nodeName,
                metricValue));
      case CLIENT_CERT_CHECK:
        return Collections.singletonList(
            buildNodeMetric(
                PlatformMetrics.HEALTH_CHECK_CLIENT_CERT_VALIDITY_DAYS,
                universe,
                nodeName,
                metricValue));
      default:
        return Collections.emptyList();
    }
  }

  private static Metric buildNodeMetric(
      PlatformMetrics metric, Universe universe, String nodeName, double value) {
    return buildMetricTemplate(metric, universe)
        .setKeyLabel(KnownAlertLabels.NODE_NAME, nodeName)
        .setValue(value);
  }
}
