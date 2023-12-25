// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableSet;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.metrics.data.AlertData;
import com.yugabyte.yw.metrics.data.AlertsResponse;
import com.yugabyte.yw.metrics.data.ResponseStatus;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Inject;
import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;
import java.util.stream.Collectors;

import static com.yugabyte.yw.common.SwamperHelper.getScrapeIntervalSeconds;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

@Singleton
public class MetricQueryHelper {

  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryHelper.class);
  public static final Integer STEP_SIZE = 100;
  public static final Integer QUERY_EXECUTOR_THREAD_POOL = 5;

  public static final String METRICS_QUERY_PATH = "query";
  public static final String ALERTS_PATH = "alerts";

  public static final String MANAGEMENT_COMMAND_RELOAD = "reload";
  private static final String PROMETHEUS_METRICS_URL_PATH = "yb.metrics.url";
  private static final String PROMETHEUS_MANAGEMENT_URL_PATH = "yb.metrics.management.url";
  public static final String PROMETHEUS_MANAGEMENT_ENABLED = "yb.metrics.management.enabled";

  private static final String CONTAINER_METRIC_PREFIX = "container";
  private static final String NODE_PREFIX = "node_prefix";
  private static final String NAMESPACE = "namespace";
  public static final String EXPORTED_INSTANCE = "exported_instance";
  public static final String TABLE_ID = "table_id";
  public static final String TABLE_NAME = "table_name";
  public static final String NAMESPACE_NAME = "namespace_name";
  private static final String POD_NAME = "pod_name";
  private static final String CONTAINER_NAME = "container_name";
  private static final String PVC = "persistentvolumeclaim";

  private static final String DEFAULT_MOUNT_POINTS = "/mnt/d[0-9]+";

  private static final Set<String> DISK_USAGE_METRICS =
      ImmutableSet.of(
          "disk_usage", "disk_usage_percent", "disk_used_size_total", "disk_capacity_size_total");
  private final Config appConfig;

  private final ApiHelper apiHelper;

  private final MetricUrlProvider metricUrlProvider;

  private final PlatformExecutorFactory platformExecutorFactory;

  @Inject
  public MetricQueryHelper(
      Config appConfig,
      ApiHelper apiHelper,
      MetricUrlProvider metricUrlProvider,
      PlatformExecutorFactory platformExecutorFactory) {
    this.appConfig = appConfig;
    this.apiHelper = apiHelper;
    this.metricUrlProvider = metricUrlProvider;
    this.platformExecutorFactory = platformExecutorFactory;
  }

  @VisibleForTesting
  public MetricQueryHelper() {
    this(null, null, null, null);
  }

  /**
   * Query prometheus for a given metricType and query params
   *
   * @param params, Query params like start, end timestamps, even filters Ex: {"metricKey":
   *     "cpu_usage_user", "start": <start timestamp>, "end": <end timestamp>}
   * @return MetricQueryResponse Object
   */
  public JsonNode query(List<String> metricKeys, Map<String, String> params) {
    HashMap<String, Map<String, String>> filterOverrides = new HashMap<>();
    List<MetricSettings> metricSettings = MetricSettings.defaultSettings(metricKeys);
    return query(metricSettings, params, filterOverrides, false);
  }

  public JsonNode query(
      List<String> metricKeys,
      Map<String, String> params,
      Map<String, Map<String, String>> filterOverrides) {
    List<MetricSettings> metricSettings = MetricSettings.defaultSettings(metricKeys);
    return query(metricSettings, params, filterOverrides, false);
  }

  public JsonNode query(Customer customer, MetricQueryParams metricQueryParams) {
    Map<String, MetricSettings> metricSettingsMap = new LinkedHashMap<>();
    if (CollectionUtils.isNotEmpty(metricQueryParams.getMetrics())) {
      metricQueryParams
          .getMetrics()
          .stream()
          .map(MetricSettings::defaultSettings)
          .forEach(
              metricSettings -> metricSettingsMap.put(metricSettings.getMetric(), metricSettings));
    }
    if (CollectionUtils.isNotEmpty(metricQueryParams.getMetricsWithSettings())) {
      metricQueryParams
          .getMetricsWithSettings()
          .forEach(
              metricSettings -> metricSettingsMap.put(metricSettings.getMetric(), metricSettings));
    }

    Map<String, String> params = new HashMap<>();
    if (metricQueryParams.getStart() != null) {
      params.put("start", String.valueOf(metricQueryParams.getStart()));
    }
    if (metricQueryParams.getEnd() != null) {
      params.put("end", String.valueOf(metricQueryParams.getEnd()));
    }
    HashMap<String, Map<String, String>> filterOverrides = new HashMap<>();
    // Given we have a limitation on not being able to rename the pod labels in
    // kubernetes cadvisor metrics, we try to see if the metric being queried is for
    // container or not, and use pod_name vs exported_instance accordingly.
    // Expect for container metrics, all the metrics would with node_prefix and exported_instance.
    boolean hasContainerMetric =
        metricSettingsMap.keySet().stream().anyMatch(s -> s.startsWith(CONTAINER_METRIC_PREFIX));
    String universeFilterLabel = hasContainerMetric ? NAMESPACE : NODE_PREFIX;
    String nodeFilterLabel = hasContainerMetric ? POD_NAME : EXPORTED_INSTANCE;

    ObjectNode filterJson = Json.newObject();
    if (StringUtils.isEmpty(metricQueryParams.getNodePrefix())) {
      String universePrefixes =
          customer
              .getUniverses()
              .stream()
              .map((universe -> universe.getUniverseDetails().nodePrefix))
              .collect(Collectors.joining("|"));
      filterJson.put(universeFilterLabel, universePrefixes);
    } else {
      final String nodePrefix = metricQueryParams.getNodePrefix();
      List<Universe> universes =
          customer
              .getUniverses()
              .stream()
              .filter(universe -> universe.getUniverseDetails().nodePrefix.equals(nodePrefix))
              .collect(Collectors.toList());
      if (universes.isEmpty()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format("No universe found with nodePrefix %s.", nodePrefix));
      }
      if (universes.size() > 1) {
        LOG.warn("Found mulitple universes with nodePrefix {}, using first one.", nodePrefix);
      }
      Universe universe = universes.get(0);

      List<NodeDetails> nodesToFilter = new ArrayList<>();
      if (CollectionUtils.isNotEmpty(metricQueryParams.getClusterUuids())
          || CollectionUtils.isNotEmpty(metricQueryParams.getRegionCodes())
          || CollectionUtils.isNotEmpty(metricQueryParams.getAvailabilityZones())
          || CollectionUtils.isNotEmpty(metricQueryParams.getNodeNames())) {
        // Need to get matching nodes
        universe
            .getNodes()
            .forEach(
                node -> {
                  if (CollectionUtils.isNotEmpty(metricQueryParams.getClusterUuids())
                      && !metricQueryParams.getClusterUuids().contains(node.placementUuid)) {
                    return;
                  }
                  if (CollectionUtils.isNotEmpty(metricQueryParams.getRegionCodes())
                      && !metricQueryParams.getRegionCodes().contains(node.cloudInfo.region)) {
                    return;
                  }
                  if (CollectionUtils.isNotEmpty(metricQueryParams.getAvailabilityZones())
                      && !metricQueryParams.getAvailabilityZones().contains(node.cloudInfo.az)) {
                    return;
                  }
                  if (CollectionUtils.isNotEmpty(metricQueryParams.getNodeNames())
                      && !metricQueryParams.getNodeNames().contains(node.getNodeName())) {
                    return;
                  }
                  nodesToFilter.add(node);
                });
        if (CollectionUtils.isEmpty(nodesToFilter)) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "No nodes found based on passed universe, "
                  + "clusters, regions, availability zones and node names");
        }
      }
      // Check if it is a Kubernetes deployment.
      if (hasContainerMetric) {

        boolean newNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

        if (CollectionUtils.isNotEmpty(nodesToFilter)) {
          Set<String> podNames = new HashSet<>();
          Set<String> containerNames = new HashSet<>();
          Set<String> pvcNames = new HashSet<>();
          Set<String> namespaces = new HashSet<>();
          for (NodeDetails node : nodesToFilter) {
            String podName = node.getK8sPodName();
            String namespace = node.getK8sNamespace();
            String containerName = podName.contains("yb-master") ? "yb-master" : "yb-tserver";
            String pvcName = String.format("(.*)-%s", podName);
            podNames.add(podName);
            containerNames.add(containerName);
            pvcNames.add(pvcName);
            namespaces.add(namespace);
          }
          filterJson.put(nodeFilterLabel, StringUtils.join(podNames, '|'));
          filterJson.put(CONTAINER_NAME, StringUtils.join(containerNames, '|'));
          filterJson.put(PVC, StringUtils.join(pvcNames, '|'));
          filterJson.put(universeFilterLabel, StringUtils.join(namespaces, '|'));
        } else {
          filterJson.put(
              universeFilterLabel, getNamespacesFilter(universe, nodePrefix, newNamingStyle));
          // Check if the universe is using newNamingStyle.
          if (newNamingStyle) {
            // TODO(bhavin192): account for max character limit in
            // Helm release name, which is 53 characters.
            // The default value in metrics.yml is yb-tserver-(.*)
            filterJson.put(nodeFilterLabel, nodePrefix + "-(.*)-yb-tserver-(.*)");
          }
        }
      } else {
        filterJson.put(universeFilterLabel, metricQueryParams.getNodePrefix());
        if (CollectionUtils.isNotEmpty(nodesToFilter)) {
          Set<String> nodeNames =
              nodesToFilter.stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
          filterJson.put(nodeFilterLabel, StringUtils.join(nodeNames, '|'));
        }

        filterOverrides.putAll(
            getFilterOverrides(customer, nodePrefix, metricSettingsMap.keySet()));
      }
    }
    if (StringUtils.isNotEmpty(metricQueryParams.getTableName())) {
      filterJson.put("table_name", metricQueryParams.getTableName());
    }
    if (StringUtils.isNotEmpty(metricQueryParams.getTableId())) {
      filterJson.put("table_id", metricQueryParams.getTableId());
    }
    if (metricQueryParams.getXClusterConfigUuid() != null) {
      XClusterConfig xClusterConfig =
          XClusterConfig.getOrBadRequest(metricQueryParams.getXClusterConfigUuid());
      String tableIdRegex = String.join("|", xClusterConfig.getTables());
      filterJson.put("table_id", tableIdRegex);
    }
    params.put("filters", Json.stringify(filterJson));
    return query(
        new ArrayList<>(metricSettingsMap.values()),
        params,
        filterOverrides,
        metricQueryParams.isRecharts());
  }

  /**
   * Query prometheus for a given metricType and query params
   *
   * @param params, Query params like start, end timestamps, even filters Ex: {"metricKey":
   *     "cpu_usage_user", "start": <start timestamp>, "end": <end timestamp>}
   * @return MetricQueryResponse Object
   */
  public JsonNode query(
      List<MetricSettings> metricsWithSettings,
      Map<String, String> params,
      Map<String, Map<String, String>> filterOverrides,
      boolean isRecharts) {
    if (metricsWithSettings.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Empty metricsWithSettings data provided.");
    }

    long scrapeInterval = getScrapeIntervalSeconds(appConfig);
    long timeDifference;
    if (params.get("end") != null) {
      timeDifference = Long.parseLong(params.get("end")) - Long.parseLong(params.get("start"));
    } else {
      String startTime = params.remove("start");
      int endTime = Math.round(DateTime.now().getMillis() / 1000);
      params.put("time", startTime);
      params.put("_", Integer.toString(endTime));
      timeDifference = endTime - Long.parseLong(startTime);
    }

    long range = Math.max(scrapeInterval * 2, timeDifference);
    params.put("range", Long.toString(range));

    String step = params.get("step");
    if (step == null) {
      if (timeDifference <= STEP_SIZE) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Should be at least " + STEP_SIZE + " seconds between start and end time");
      }
      long resolution = Math.max(scrapeInterval * 2, Math.round(timeDifference / STEP_SIZE));
      params.put("step", String.valueOf(resolution));
    } else {
      try {
        if (Integer.parseInt(step) <= 0) {
          throw new PlatformServiceException(BAD_REQUEST, "Step should not be less than 1 second");
        }
      } catch (NumberFormatException nfe) {
        throw new PlatformServiceException(BAD_REQUEST, "Step should be a valid integer");
      }
    }

    // Adjust the start time so the graphs are consistent for different requests.
    if (params.get("start") != null) {
      long startTime = Long.parseLong(params.get("start"));
      long adjustingRemainder = startTime % Long.parseLong(params.get("step"));
      long adjustedStartTime = startTime - adjustingRemainder;
      params.put("start", Long.toString(adjustedStartTime));
      if (params.get("end") != null) {
        long adjustedEndTime = Long.parseLong(params.get("end")) - adjustingRemainder;
        params.put("end", Long.toString(adjustedEndTime));
      }
    }

    HashMap<String, String> additionalFilters = new HashMap<>();
    if (params.containsKey("filters")) {
      try {
        additionalFilters = new ObjectMapper().readValue(params.get("filters"), HashMap.class);
      } catch (IOException e) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Invalid filter params provided, it should be a hash.");
      }
    }

    String metricsUrl = appConfig.getString(PROMETHEUS_METRICS_URL_PATH);
    if ((null == metricsUrl || metricsUrl.isEmpty())) {
      LOG.error("Error fetching metrics data: no prometheus metrics URL configured");
      return Json.newObject();
    }

    ExecutorService threadPool =
        platformExecutorFactory.createFixedExecutor(
            getClass().getSimpleName(),
            QUERY_EXECUTOR_THREAD_POOL,
            Executors.defaultThreadFactory());
    try {
      Set<Future<JsonNode>> futures = new HashSet<Future<JsonNode>>();
      for (MetricSettings metricSettings : metricsWithSettings) {
        Map<String, String> queryParams = params;
        queryParams.put("queryKey", metricSettings.getMetric());

        Map<String, String> metricAdditionalFilters =
            filterOverrides.getOrDefault(metricSettings.getMetric(), new HashMap<>());
        metricAdditionalFilters.putAll(additionalFilters);

        Callable<JsonNode> callable =
            new MetricQueryExecutor(
                metricUrlProvider,
                apiHelper,
                queryParams,
                metricAdditionalFilters,
                metricSettings,
                isRecharts);
        Future<JsonNode> future = threadPool.submit(callable);
        futures.add(future);
      }

      ObjectNode responseJson = Json.newObject();
      for (Future<JsonNode> future : futures) {
        JsonNode response = Json.newObject();
        try {
          response = future.get();
          responseJson.set(response.get("queryKey").asText(), response);
        } catch (InterruptedException | ExecutionException e) {
          LOG.error("Error fetching metrics data", e);
        }
      }
      return responseJson;
    } finally {
      threadPool.shutdown();
    }
  }

  /**
   * Query Prometheus via HTTP for metric values
   *
   * <p>The main difference between this and regular MetricQueryHelper::query is that it does not
   * depend on the metric config being present in metrics.yml
   *
   * <p>promQueryExpression is a standard prom query expression of the form
   *
   * <p>metric_name{filter_name_optional="filter_value"}[time_expr_optional]
   *
   * <p>for ex: 'up', or 'up{node_prefix="yb-test"}[10m] Without a time expression, only the most
   * recent value is returned.
   *
   * <p>The return type is a set of labels for each metric and an array of time-stamped values
   */
  public ArrayList<MetricQueryResponse.Entry> queryDirect(String promQueryExpression) {
    final String queryUrl = getPrometheusQueryUrl(METRICS_QUERY_PATH);

    HashMap<String, String> getParams = new HashMap<>();
    getParams.put("query", promQueryExpression);
    final JsonNode responseJson =
        apiHelper.getRequest(queryUrl, new HashMap<>(), /*headers*/ getParams);
    final MetricQueryResponse metricResponse =
        Json.fromJson(responseJson, MetricQueryResponse.class);
    if (metricResponse.error != null || metricResponse.data == null) {
      throw new RuntimeException("Error querying prometheus metrics: " + responseJson.toString());
    }

    return metricResponse.getValues();
  }

  public List<AlertData> queryAlerts() {
    final String queryUrl = getPrometheusQueryUrl(ALERTS_PATH);

    final JsonNode responseJson = apiHelper.getRequest(queryUrl);
    final AlertsResponse response = Json.fromJson(responseJson, AlertsResponse.class);
    if (response.getStatus() != ResponseStatus.success) {
      throw new RuntimeException("Error querying prometheus alerts: " + response);
    }

    if (response.getData() == null || response.getData().getAlerts() == null) {
      return Collections.emptyList();
    }
    return response.getData().getAlerts();
  }

  public void postManagementCommand(String command) {
    final String queryUrl = getPrometheusManagementUrl(command);
    if (!apiHelper.postRequest(queryUrl)) {
      throw new RuntimeException(
          "Failed to perform " + command + " on prometheus instance " + queryUrl);
    }
  }

  public boolean isPrometheusManagementEnabled() {
    return appConfig.getBoolean(PROMETHEUS_MANAGEMENT_ENABLED);
  }

  private String getPrometheusManagementUrl(String path) {
    final String prometheusManagementUrl = appConfig.getString(PROMETHEUS_MANAGEMENT_URL_PATH);
    if (StringUtils.isEmpty(prometheusManagementUrl)) {
      throw new RuntimeException(PROMETHEUS_MANAGEMENT_URL_PATH + " not set");
    }
    return prometheusManagementUrl + "/" + path;
  }

  private String getPrometheusQueryUrl(String path) {
    final String metricsUrl = appConfig.getString(PROMETHEUS_METRICS_URL_PATH);
    if (StringUtils.isEmpty(metricsUrl)) {
      throw new RuntimeException(PROMETHEUS_METRICS_URL_PATH + " not set");
    }
    return metricsUrl + "/" + path;
  }

  // Return a regex string for filtering the metrics based on
  // namespaces of the given universe and nodePrefix. Should be used
  // for Kubernetes universes only.
  private String getNamespacesFilter(Universe universe, String nodePrefix, boolean newNamingStyle) {
    // We need to figure out the correct namespace for each AZ.  We do
    // that by getting the the universe's provider and then go through
    // the azConfigs.
    List<String> namespaces = new ArrayList<>();

    for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      for (Region r : Region.getByProvider(provider.uuid)) {
        for (AvailabilityZone az : AvailabilityZone.getAZsForRegion(r.uuid)) {
          boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
          namespaces.add(
              KubernetesUtil.getKubernetesNamespace(
                  isMultiAZ,
                  nodePrefix,
                  az.code,
                  az.getUnmaskedConfig(),
                  newNamingStyle,
                  cluster.clusterType == ClusterType.ASYNC));
        }
      }
    }

    return String.join("|", namespaces);
  }

  private HashMap<String, HashMap<String, String>> getFilterOverrides(
      Customer customer, String nodePrefix, Set<String> metricNames) {

    HashMap<String, HashMap<String, String>> filterOverrides = new HashMap<>();
    // For a disk usage metric query, the mount point has to be modified to match the actual
    // mount point for an onprem universe.
    for (String metricName : metricNames) {
      if (DISK_USAGE_METRICS.contains(metricName)) {
        List<Universe> universes =
            customer
                .getUniverses()
                .stream()
                .filter(
                    u ->
                        u.getUniverseDetails().nodePrefix != null
                            && u.getUniverseDetails().nodePrefix.equals(nodePrefix))
                .collect(Collectors.toList());
        if (CollectionUtils.isEmpty(universes)) {
          LOG.warn(
              "Failed to find universe with node prefix {}, will not add mount point filter",
              nodePrefix);
          return filterOverrides;
        }
        HashMap<String, String> mountFilters = new HashMap<>();
        mountFilters.put("mountpoint", getMountPoints(universes.get(0)));
        filterOverrides.put(metricName, mountFilters);
      }
    }
    return filterOverrides;
  }

  private static boolean checkNonNullMountRoots(NodeDetails n) {
    return n.cloudInfo != null
        && n.cloudInfo.mount_roots != null
        && !n.cloudInfo.mount_roots.isEmpty();
  }

  public static String getMountPoints(Universe universe) {
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType
        == CloudType.onprem) {
      final String mountRoots =
          universe
              .getNodes()
              .stream()
              .filter(MetricQueryHelper::checkNonNullMountRoots)
              .map(n -> n.cloudInfo.mount_roots)
              .findFirst()
              .orElse("");
      // TODO: technically, this code is based on the primary cluster being onprem
      // and will return inaccurate results if the universe has a read replica that is
      // not onprem.
      if (!mountRoots.isEmpty()) {
        HashMap<String, String> mountFilters = new HashMap<>();
        return mountRoots;
      } else {
        LOG.debug(
            "No mount points found in onprem universe {}",
            universe.getUniverseDetails().nodePrefix);
      }
    }
    return DEFAULT_MOUNT_POINTS;
  }
}
