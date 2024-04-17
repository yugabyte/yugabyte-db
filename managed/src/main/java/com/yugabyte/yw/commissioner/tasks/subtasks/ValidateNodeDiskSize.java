// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.google.common.collect.Iterables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;

/** This performs best-effort disk space validation based on average disk sizes. */
@Slf4j
public class ValidateNodeDiskSize extends UniverseDefinitionTaskBase {
  private static final String DB_DISK_USAGE_QUERY_FORMAT =
      "avg(sum by (exported_instance)(log_wal_size{node_prefix=\"%s\"}) + sum by"
          + " (exported_instance)(rocksdb_current_version_sst_files_size{node_prefix=\"%s\"}))/"
          + " 1073741824";

  private static final String DB_DISK_USAGE_DEDICATED_NODES_QUERY_FORMAT =
      "avg(sum by (exported_instance)"
          + " (log_wal_size{node_prefix=\"%s\",export_type=\"%s_export\"}) +"
          + " sum by (exported_instance)(rocksdb_current_version_sst_files_size"
          + "{node_prefix=\"%s\",export_type=\"%s_export\"}))/"
          + " 1073741824";

  private static final String DB_DISK_USAGE_QUERY_FORMAT_K8S =
      "sum(kubelet_volume_stats_used_bytes{namespace=~\"%s\","
          + " persistentvolumeclaim=~\"(.*)-yb-%s-(.*)\"})/1073741824";

  // When this check is run, the ToBeAdded nodes are not yet added to the universe and the root
  // mount points are unknown for on-prem nodes.
  private static final String DISK_FREE_QUERY_FORMAT =
      "sum by (exported_instance,"
          + " mountpoint)(node_filesystem_free_bytes{node_prefix=\"%s\"})/1073741824";

  private final MetricQueryHelper metricQueryHelper;

  @Inject
  protected ValidateNodeDiskSize(
      BaseTaskDependencies baseTaskDependencies, MetricQueryHelper metricQueryHelper) {
    super(baseTaskDependencies);
    this.metricQueryHelper = metricQueryHelper;
  }

  @JsonDeserialize(converter = Params.Converter.class)
  public static class Params extends UniverseDefinitionTaskParams {
    public UUID clusterUuid;
    // Percentage of current disk usage that may consume on the target nodes.
    public int targetDiskUsagePercentage;
    // Whether tservers changed
    public boolean tserversChanged;
    // Whether masters changed
    public boolean mastersChanged;

    /* Param specific to K8s */
    // For k8s disk query, we need the namespace of tservers
    public Set<String> namespaces;

    public static class Converter extends BaseConverter<Params> {}
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private double fetchAvgDiskUsedSize(
      CloudType cloudType, ServerType serverType, int numCurrentNodes) {
    String query;
    if (cloudType == CloudType.kubernetes) {
      query =
          String.format(
              DB_DISK_USAGE_QUERY_FORMAT_K8S,
              StringUtils.join(taskParams().namespaces, "|"),
              serverType == ServerType.MASTER ? "master" : "tserver");
    } else if (serverType != ServerType.EITHER) {
      boolean isMaster = serverType.equals(ServerType.MASTER);
      String exportType = isMaster ? "master" : "tserver";
      query =
          String.format(
              DB_DISK_USAGE_DEDICATED_NODES_QUERY_FORMAT,
              taskParams().nodePrefix,
              exportType,
              taskParams().nodePrefix,
              exportType);
    } else {
      query =
          String.format(
              DB_DISK_USAGE_QUERY_FORMAT, taskParams().nodePrefix, taskParams().nodePrefix);
    }
    log.info("Running query: {}", query);
    List<MetricQueryResponse.Entry> responseList = null;
    try {
      responseList = metricQueryHelper.queryDirect(query);
    } catch (RuntimeException e) {
      log.error("Failed to run metrics query {} - {}", query, e.getMessage());
    }
    if (CollectionUtils.isEmpty(responseList)) {
      log.info("No metrics fetched for query: {}", query);
      return 0.0;
    }
    ImmutablePair<Double, Double> pair = Iterables.getFirst(responseList.get(0).values, null);
    if (pair == null) {
      String errMsg = String.format("No response for query %s", query);
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    double size = pair.getRight();
    if (cloudType == CloudType.kubernetes) {
      size /= numCurrentNodes;
    }
    return size;
  }

  private double fetchAvgDiskFreeSize(Map<String, Set<String>> nodeMountPoints) {
    String query = String.format(DISK_FREE_QUERY_FORMAT, taskParams().nodePrefix);
    log.info("Running query: {}", query);
    List<MetricQueryResponse.Entry> responseList = null;
    try {
      responseList = metricQueryHelper.queryDirect(query);
    } catch (RuntimeException e) {
      log.error("Failed to run metrics query {} - {}", query, e.getMessage());
    }
    if (CollectionUtils.isEmpty(responseList)) {
      log.info("No metrics fetched for query: {}", query);
      return 0.0;
    }
    double total = 0.0;
    int count = 0;
    for (MetricQueryResponse.Entry entry : responseList) {
      String nodeName = entry.labels.get("exported_instance");
      String mountPoint = entry.labels.get("mountpoint");
      Set<String> mountPoints = nodeMountPoints.get(nodeName);
      if (CollectionUtils.isEmpty(mountPoints)) {
        log.debug("Skipping node {}", nodeName);
        continue;
      }
      if (!mountPoints.stream()
          .map(m -> Paths.get(m))
          .anyMatch(p -> p.startsWith(Paths.get(mountPoint)))) {
        log.info("Unmatched mount points {} for node {}", mountPoints, nodeName);
        continue;
      }
      ImmutablePair<Double, Double> pair = Iterables.getFirst(entry.values, null);
      if (pair == null) {
        String errMsg = String.format("No response for query %s", query);
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }
      total += pair.getRight();
      count++;
    }
    return count == 0 ? total : total / count;
  }

  private void validateNodeDiskSize(Cluster cluster, ServerType serverType) {
    CloudType cloudType = cluster.userIntent.providerType;
    Function<NodeDetails, Boolean> serverTypeFilter =
        (nD) ->
            serverType.equals(ServerType.EITHER)
                ? true
                : serverType.equals(ServerType.MASTER) ? nD.isMaster : nD.isTserver;

    Set<NodeDetails> clusterNodes =
        taskParams().getNodesInCluster(cluster.uuid).stream()
            .filter(serverTypeFilter::apply)
            .collect(Collectors.toSet());
    int totalCurrentNodes =
        (int) clusterNodes.stream().filter(n -> n.state != NodeState.ToBeAdded).count();
    int totalTargetNodes =
        (int) clusterNodes.stream().filter(n -> n.state != NodeState.ToBeRemoved).count();

    double avgCurrentDiskUsage = fetchAvgDiskUsedSize(cloudType, serverType, totalCurrentNodes);
    if (avgCurrentDiskUsage == 0.0) {
      log.info("Average disk usage is 0.00 GB. Skipping disk validation");
      return;
    }
    double totalCurrentDiskUsage = avgCurrentDiskUsage * totalCurrentNodes;
    double totalTargetDiskUsage = avgCurrentDiskUsage * totalTargetNodes;
    double totalTargetDiskSizeNeeded =
        (totalCurrentDiskUsage * taskParams().targetDiskUsagePercentage) / 100;
    // Additional disk size needed to distribute the surplus.
    double additionalDiskSizeNeeded = totalTargetDiskSizeNeeded - totalTargetDiskUsage;
    double avgDiskFreeSize = 0.0;
    if (cloudType == CloudType.onprem) {
      // Fetch the average free disk size per node. ToBeAdded nodes are automatically excluded as
      // they do not belong to the universe as this is run before freezing.
      Map<String, Set<String>> rootMounts =
          getOnpremNodeMountPoints(
              cluster, n -> serverTypeFilter.apply(n) && n.state != NodeState.ToBeAdded);
      log.debug("Root mount points are {}", rootMounts);
      avgDiskFreeSize = fetchAvgDiskFreeSize(rootMounts);
    }
    // If the volumes already have some non-db data, total disk size cannot be used to compare.
    // It is better to compare the additional required disk size against the total free size.
    double totalTargetDiskFreeSize = 0.0;
    for (NodeDetails node : clusterNodes) {
      if (node.state == NodeState.ToBeRemoved) {
        continue;
      }
      if (node.state == NodeState.ToBeAdded) {
        // For cloud, get the size from the config as this can change.
        // For on-prem, average usage is added to the average free to arrive at the total estimate.
        totalTargetDiskFreeSize +=
            (cloudType == CloudType.onprem)
                ? (avgDiskFreeSize + avgCurrentDiskUsage)
                : fetchDiskSizeLocally(cluster, node);
      } else {
        // Free size can become -ve if it is a downsize.
        totalTargetDiskFreeSize +=
            (cloudType == CloudType.onprem)
                ? avgDiskFreeSize
                : (fetchDiskSizeLocally(cluster, node) - avgCurrentDiskUsage);
      }
    }
    String msg =
        String.format(
            "For %s: Total additional disk size: %,.2f GB, total available size: %,.2f GB",
            serverType, additionalDiskSizeNeeded, totalTargetDiskFreeSize);
    log.info(msg);
    if (additionalDiskSizeNeeded > totalTargetDiskFreeSize) {
      String errMsg =
          String.format(
              "For %s: Additional disk size of %,.2f GB is needed, but only %,.2f GB is available",
              serverType, additionalDiskSizeNeeded, Math.max(0.0, totalTargetDiskFreeSize));
      throw new RuntimeException(errMsg);
    }
  }

  private double fetchDiskSizeLocally(Cluster cluster, NodeDetails node) {
    // The method getDeviceInfoForNode takes care of dedicated masters case
    DeviceInfo deviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
    return deviceInfo.volumeSize == null ? -1.0 : deviceInfo.volumeSize;
  }

  private Map<String, Set<String>> getOnpremNodeMountPoints(
      Cluster cluster, Predicate<NodeDetails> filter) {
    final Map<String, Set<String>> nodeMountPoints = new HashMap<>();
    taskParams().getNodesInCluster(cluster.uuid).stream()
        .filter(n -> filter.test(n))
        .forEach(
            n -> {
              UserIntent userIntent = taskParams().getClusterByUuid(n.placementUuid).userIntent;
              DeviceInfo deviceInfo = userIntent.getDeviceInfoForNode(n);
              if (deviceInfo != null && StringUtils.isNotEmpty(deviceInfo.mountPoints)) {
                nodeMountPoints.put(
                    n.getNodeName(),
                    Arrays.stream(deviceInfo.mountPoints.split(",")).collect(Collectors.toSet()));
              } else {
                log.warn("Device info is missing for node {}", n.getNodeName());
              }
            });
    return nodeMountPoints;
  }

  @Override
  public void run() {
    Cluster cluster = taskParams().getClusterByUuid(taskParams().clusterUuid);
    CloudType cloudType = cluster.userIntent.providerType;
    boolean isDedicated = cluster.userIntent.dedicatedNodes;
    if ((cloudType == CloudType.kubernetes) || isDedicated) {
      if (taskParams().mastersChanged) {
        validateNodeDiskSize(cluster, ServerType.MASTER);
      }
      if (taskParams().tserversChanged) {
        validateNodeDiskSize(cluster, ServerType.TSERVER);
      }
    } else {
      validateNodeDiskSize(cluster, ServerType.EITHER);
    }
  }
}
