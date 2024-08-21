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

import static com.yugabyte.yw.common.metrics.MetricService.DEFAULT_METRIC_EXPIRY_SEC;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowPlusWithoutMillis;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck.Details;
import com.yugabyte.yw.models.HealthCheck.Details.NodeData;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.temporal.ChronoUnit;
import java.util.Collections;
import java.util.List;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class HealthCheckMetrics {
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
  static final String OPENED_FILE_DESCRIPTORS_CHECK = "Opened file descriptors";
  static final String CLOCK_SYNC_CHECK = "Clock synchronization";
  private static final String NODE_TO_NODE_CA_CERT_CHECK = "Node To Node CA Cert Expiry Days";
  private static final String NODE_TO_NODE_CERT_CHECK = "Node To Node Cert Expiry Days";
  private static final String CLIENT_TO_NODE_CA_CERT_CHECK = "Client To Node CA Cert Expiry Days";
  private static final String CLIENT_TO_NODE_CERT_CHECK = "Client To Node Cert Expiry Days";
  private static final String CLIENT_CA_CERT_CHECK = "Client CA Cert Expiry Days";
  private static final String CLIENT_CERT_CHECK = "Client Cert Expiry Days";
  public static final String NODE_EXPORTER_CHECK = "Node exporter";
  private static final String YB_CONTROLLER_CHECK = "YB-Controller server check";

  public static final String CUSTOM_NODE_METRICS_COLLECTION_METRIC = "yb_node_custom_node_metrics";

  public static final List<PlatformMetrics> HEALTH_CHECK_METRICS_WITHOUT_STATUS =
      ImmutableList.<PlatformMetrics>builder()
          .add(PlatformMetrics.YB_UNIV_HEALTH_STATUS)
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
          .add(PlatformMetrics.HEALTH_CHECK_YB_CONTROLLER_DOWN)
          .build();

  public static final List<PlatformMetrics> HEALTH_CHECK_METRICS =
      ImmutableList.<PlatformMetrics>builder()
          .add(PlatformMetrics.HEALTH_CHECK_STATUS)
          .addAll(HEALTH_CHECK_METRICS_WITHOUT_STATUS)
          .build();

  private HealthCheckMetrics() {}

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
      case YB_CONTROLLER_CHECK:
        return PlatformMetrics.HEALTH_CHECK_YB_CONTROLLER_DOWN;
      default:
        return null;
    }
  }

  public static List<Metric> getNodeMetrics(
      Customer customer, Universe universe, NodeData nodeData, List<Details.Metric> metrics) {
    if (CollectionUtils.isEmpty(metrics)) {
      return Collections.emptyList();
    }
    return metrics.stream()
        .flatMap(m -> buildNodeMetric(m, customer, universe, nodeData).stream())
        .collect(Collectors.toList());
  }

  private static List<Metric> buildNodeMetric(
      Details.Metric metric, Customer customer, Universe universe, NodeData nodeData) {
    if (CollectionUtils.isEmpty(metric.getValues())) {
      return Collections.emptyList();
    }
    return metric.getValues().stream()
        .map(
            value -> {
              Metric result =
                  new Metric()
                      .setExpireTime(
                          nowPlusWithoutMillis(DEFAULT_METRIC_EXPIRY_SEC, ChronoUnit.SECONDS))
                      .setType(Metric.Type.GAUGE)
                      .setName(metric.getName())
                      .setHelp(metric.getHelp())
                      .setUnit(metric.getUnit())
                      .setCustomerUUID(customer.getUuid())
                      .setSourceUuid(universe.getUniverseUUID())
                      .setLabels(
                          MetricLabelsBuilder.create().appendSource(universe).getMetricLabels())
                      .setKeyLabel(KnownAlertLabels.NODE_NAME, nodeData.getNodeName())
                      .setLabel(KnownAlertLabels.NODE_ADDRESS, nodeData.getNode())
                      .setLabel(KnownAlertLabels.NODE_IDENTIFIER, nodeData.getNodeIdentifier())
                      .setValue(value.getValue());
              if (CollectionUtils.isNotEmpty(value.getLabels())) {
                value
                    .getLabels()
                    .forEach(label -> result.setKeyLabel(label.getName(), label.getValue()));
              }
              return result;
            })
        .collect(Collectors.toList());
  }

  public static Metric buildLegacyNodeMetric(
      Customer customer, Universe universe, String node, String checkName, Double value) {
    return new Metric()
        .setExpireTime(nowPlusWithoutMillis(DEFAULT_METRIC_EXPIRY_SEC, ChronoUnit.SECONDS))
        .setType(Metric.Type.GAUGE)
        .setName(PlatformMetrics.YB_UNIV_HEALTH_STATUS.getMetricName())
        .setHelp("Boolean result of health checks")
        .setCustomerUUID(customer.getUuid())
        .setSourceUuid(universe.getUniverseUUID())
        .setLabel(kUnivNameLabel, universe.getName())
        .setLabel(kUnivUUIDLabel, universe.getUniverseUUID().toString())
        .setKeyLabel(kNodeLabel, node)
        .setKeyLabel(kCheckLabel, checkName)
        .setValue(value);
  }
}
