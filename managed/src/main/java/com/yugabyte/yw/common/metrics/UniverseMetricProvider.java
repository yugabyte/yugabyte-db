/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import static com.yugabyte.yw.common.metrics.MetricService.STATUS_OK;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.common.collect.ImmutableList;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class UniverseMetricProvider implements MetricsProvider {

  private static final List<PlatformMetrics> UNIVERSE_METRICS =
      ImmutableList.of(
          PlatformMetrics.UNIVERSE_EXISTS,
          PlatformMetrics.UNIVERSE_PAUSED,
          PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_NODE_FUNCTION);

  @Override
  public List<MetricSaveGroup> getMetricGroups() throws Exception {
    List<MetricSaveGroup> metricSaveGroups = new ArrayList<>();
    for (Customer customer : Customer.getAll()) {
      for (Universe universe : Universe.getAllWithoutResources(customer)) {
        MetricSaveGroup.MetricSaveGroupBuilder universeGroup = MetricSaveGroup.builder();
        universeGroup.metric(
            createUniverseMetric(customer, universe, PlatformMetrics.UNIVERSE_EXISTS, STATUS_OK));
        universeGroup.metric(
            createUniverseMetric(
                customer,
                universe,
                PlatformMetrics.UNIVERSE_PAUSED,
                statusValue(universe.getUniverseDetails().universePaused)));
        universeGroup.metric(
            createUniverseMetric(
                customer,
                universe,
                PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS,
                statusValue(universe.getUniverseDetails().updateInProgress)));
        universeGroup.metric(
            createUniverseMetric(
                customer,
                universe,
                PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS,
                statusValue(universe.getUniverseDetails().backupInProgress)));

        if (universe.getUniverseDetails().nodeDetailsSet != null) {
          for (NodeDetails nodeDetails : universe.getUniverseDetails().nodeDetailsSet) {
            if (nodeDetails.cloudInfo == null || nodeDetails.cloudInfo.private_ip == null) {
              log.warn(
                  "Universe {} does not seem to be created correctly"
                      + " - skipping per-node metrics",
                  universe.getUniverseUUID());
              break;
            }

            String ipAddress = nodeDetails.cloudInfo.private_ip;
            universeGroup.metric(
                createNodeMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                    ipAddress,
                    nodeDetails.masterHttpPort,
                    "master_export",
                    statusValue(nodeDetails.isMaster)));
            universeGroup.metric(
                createNodeMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                    ipAddress,
                    nodeDetails.tserverHttpPort,
                    "tserver_export",
                    statusValue(nodeDetails.isTserver)));
            universeGroup.metric(
                createNodeMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                    ipAddress,
                    nodeDetails.ysqlServerHttpPort,
                    "ysql_export",
                    statusValue(nodeDetails.isYsqlServer)));
            universeGroup.metric(
                createNodeMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                    ipAddress,
                    nodeDetails.yqlServerHttpPort,
                    "cql_export",
                    statusValue(nodeDetails.isYqlServer)));
            universeGroup.metric(
                createNodeMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                    ipAddress,
                    nodeDetails.redisServerHttpPort,
                    "redis_export",
                    statusValue(nodeDetails.isRedisServer)));
          }
        }
        universeGroup.cleanMetricFilter(
            MetricFilter.builder()
                .metricNames(UNIVERSE_METRICS)
                .sourceUuid(universe.getUniverseUUID())
                .build());
        metricSaveGroups.add(universeGroup.build());
      }
    }
    return metricSaveGroups;
  }

  private Metric createUniverseMetric(
      Customer customer, Universe universe, PlatformMetrics metric, double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    return buildMetricTemplate(metric, customer, universe)
        .setLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
        .setValue(value);
  }

  private Metric createNodeMetric(
      Customer customer,
      Universe universe,
      PlatformMetrics metric,
      String ipAddress,
      int port,
      String exportType,
      double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    return buildMetricTemplate(metric, customer, universe)
        .setKeyLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
        .setKeyLabel(KnownAlertLabels.INSTANCE, ipAddress + ":" + port)
        .setLabel(KnownAlertLabels.EXPORT_TYPE, exportType)
        .setValue(value);
  }

  @Override
  public String getName() {
    return "Universe metrics";
  }
}
