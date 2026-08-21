// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.metrics.MetricService.STATUS_NOT_OK;
import static com.yugabyte.yw.common.metrics.MetricService.STATUS_OK;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ConnectivityStateResponse;
import org.yb.client.YBClient;
import org.yb.tserver.TserverService.ConnectivityEntryPB;

@Slf4j
public class ConnectivityChecker {

  private static final List<PlatformMetrics> CONNECTIVITY_METRICS =
      Arrays.asList(PlatformMetrics.UNIVERSE_NODE_CONNECTIVITY_STATUS);

  private final Map<UUID, Future<?>> runningChecks = new ConcurrentHashMap<>();

  private final ExecutorService connectivityCheckExecutor;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybClientService;
  private final MetricService metricService;

  public ConnectivityChecker(
      ExecutorService connectivityCheckExecutor,
      RuntimeConfGetter confGetter,
      YBClientService ybClientService,
      MetricService metricService) {
    this.connectivityCheckExecutor = connectivityCheckExecutor;
    this.confGetter = confGetter;
    this.ybClientService = ybClientService;
    this.metricService = metricService;
  }

  public void processAll() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping connectivity check for follower platform");
      return;
    }
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableConnectivityMetricCollection)) {
      log.debug("Connectivity metric collection is disabled");
      return;
    }
    try {
      for (Customer customer : Customer.getAll()) {
        for (Universe universe : Universe.getAllWithoutResources(customer)) {
          if (universe.universeIsLocked()) {
            continue;
          }
          if (universe.getUniverseDetails() == null
              || universe.getUniverseDetails().getPrimaryCluster() == null
              || universe.getUniverseDetails().getPrimaryCluster().userIntent == null) {
            continue;
          }
          if (Util.compareYBVersions(
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
                  "2026.1.0.0-b1",
                  "2.29.0.0-b570",
                  true)
              < 0) {
            continue;
          }

          Future<?> curFuture = runningChecks.get(universe.getUniverseUUID());
          if (curFuture != null) {
            log.debug(
                "Connectivity check already in progress for {}, skipping",
                universe.getUniverseUUID());
            continue;
          }

          final Customer c = customer;
          try {
            runningChecks.put(
                universe.getUniverseUUID(),
                connectivityCheckExecutor.submit(
                    () -> {
                      try {
                        collectAndSaveConnectivityMetrics(c, universe);
                      } finally {
                        runningChecks.remove(universe.getUniverseUUID());
                      }
                    }));
          } catch (RuntimeException e) {
            log.error(
                "Failed to submit connectivity check for universe {}",
                universe.getUniverseUUID(),
                e);
          }
        }
      }
    } catch (Exception e) {
      log.error("Failed to process connectivity checks", e);
    }
  }

  @VisibleForTesting
  void collectAndSaveConnectivityMetrics(Customer customer, Universe universe) {
    try {
      List<NodeDetails> tservers =
          universe.getTServers().stream()
              .filter(node -> node.state == NodeDetails.NodeState.Live)
              .collect(Collectors.toList());
      if (tservers == null || tservers.isEmpty()) {
        return;
      }

      String nodePrefix = universe.getUniverseDetails().nodePrefix;
      List<Metric> nodeMetrics = new ArrayList<>();

      try (YBClient client = ybClientService.getUniverseClient(universe)) {
        for (NodeDetails node : tservers) {
          if (node.cloudInfo == null || node.nodeUuid == null) {
            continue;
          }
          String host =
              node.cloudInfo.private_ip != null
                  ? node.cloudInfo.private_ip
                  : node.cloudInfo.private_dns;
          if (host == null || host.isEmpty()) {
            continue;
          }
          HostAndPort hp = HostAndPort.fromParts(host, node.tserverRpcPort);
          boolean healthy = false;
          try {
            ConnectivityStateResponse response = client.getConnectivityState(hp);
            if (response.hasError()) {
              log.warn(
                  "ConnectivityState response has error for node {}: {}",
                  node.nodeName,
                  response.getError());
              continue;
            }
            healthy = true;
            for (ConnectivityEntryPB entry : response.getEntries()) {
              String peerUuid = entry.getUuid();
              if (peerUuid == null) {
                continue;
              }
              String peerEndpoint = entry.hasEndpoint() ? entry.getEndpoint().getHost() : "unknown";
              if (!entry.getAlive()) {
                log.debug(
                    "Node {} reports peer {} (uuid={}) as not alive",
                    node.nodeName,
                    peerEndpoint,
                    peerUuid);
                continue;
              }
              boolean peerHasIssue =
                  entry.getLastFailure() != null && !entry.getLastFailure().isEmpty();
              healthy = healthy && !peerHasIssue;
              if (peerHasIssue) {
                log.warn(
                    "Node {} reports peer {} (uuid={}) as unreachable: {}",
                    node.nodeName,
                    peerEndpoint,
                    peerUuid,
                    entry.getLastFailure());
              }
            }
          } catch (Exception e) {
            log.warn("ConnectivityState RPC failed for node {}: {}", node.nodeName, e.getMessage());
          }
          nodeMetrics.add(
              buildMetricTemplate(
                      PlatformMetrics.UNIVERSE_NODE_CONNECTIVITY_STATUS, customer, universe)
                  .setKeyLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
                  .setKeyLabel(
                      KnownAlertLabels.INSTANCE,
                      node.cloudInfo.private_ip + ":" + node.tserverRpcPort)
                  .setLabel(KnownAlertLabels.NODE_NAME, node.nodeName)
                  .setLabel(KnownAlertLabels.NODE_ADDRESS, node.cloudInfo.private_ip)
                  .setValue(healthy ? STATUS_OK : STATUS_NOT_OK));
        }
      }

      MetricFilter cleanFilter =
          MetricFilter.builder()
              .metricNames(CONNECTIVITY_METRICS)
              .sourceUuid(universe.getUniverseUUID())
              .build();
      metricService.cleanAndSave(nodeMetrics, cleanFilter);
      metricService.setOkStatusMetric(
          buildMetricTemplate(
              PlatformMetrics.UNIVERSE_CONNECTIVITY_METRIC_PROCESSOR_STATUS, customer, universe));
    } catch (Exception e) {
      log.error(
          "Connectivity metric collection failed for universe {}: ", universe.getUniverseUUID(), e);
      metricService.setFailureStatusMetric(
          buildMetricTemplate(
              PlatformMetrics.UNIVERSE_CONNECTIVITY_METRIC_PROCESSOR_STATUS, customer, universe));
    }
  }
}
