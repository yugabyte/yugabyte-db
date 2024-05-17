package com.yugabyte.troubleshoot.ts.service;

import static com.yugabyte.troubleshoot.ts.MetricsUtil.LABEL_RESULT;
import static com.yugabyte.troubleshoot.ts.MetricsUtil.buildSummary;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.troubleshoot.ts.logs.LogsUtil;
import com.yugabyte.troubleshoot.ts.models.*;
import io.prometheus.client.Summary;
import java.io.IOException;
import java.time.*;
import java.util.*;
import java.util.concurrent.Future;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.springframework.core.io.ClassPathResource;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Service;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;

@Slf4j
@Service
public class GraphService {

  static String LABEL_GRAPH_NAME = "graph_name";

  static final Summary QUERY_TIME =
      buildSummary(
          "ts_graph_query_time_millis", "Graph query time", LABEL_RESULT, LABEL_GRAPH_NAME);

  static final Summary DATA_RETRIEVAL_TIME =
      buildSummary(
          "ts_graph_data_retrieval_time_millis", "Graph data retrieval time", LABEL_GRAPH_NAME);

  private static final String CONTAINER_METRIC_PREFIX = "container";

  private static final Set<String> DATA_DISK_USAGE_METRICS =
      ImmutableSet.of(
          "disk_usage", "disk_used_size_total", "disk_capacity_size_total", "disk_usage_percent");

  private static final Set<String> DISK_USAGE_METRICS =
      ImmutableSet.<String>builder()
          .addAll(DATA_DISK_USAGE_METRICS)
          .add("disk_volume_usage_percent")
          .add("disk_volume_used")
          .add("disk_volume_capacity")
          .build();

  public static final Integer GRAPH_POINTS_DEFAULT = 100;
  private final ThreadPoolTaskExecutor metricQueryExecutor;
  private final List<GraphSourceIF> sources = new ArrayList<>();

  private final UniverseMetadataService universeMetadataService;
  private final UniverseDetailsService universeDetailsService;

  @SneakyThrows
  public GraphService(
      ThreadPoolTaskExecutor metricQueryExecutor,
      TsStorageGraphService tsStorageGraphService,
      MetricGraphService metricGraphService,
      UniverseMetadataService universeMetadataService,
      UniverseDetailsService universeDetailsService) {
    this.metricQueryExecutor = metricQueryExecutor;
    this.universeMetadataService = universeMetadataService;
    this.universeDetailsService = universeDetailsService;
    this.sources.add(tsStorageGraphService);
    this.sources.add(metricGraphService);
  }

  static <T> Map<String, T> fillConfigMap(
      ObjectMapper mapper, String resource, Class<T> configClass) throws IOException {

    LoaderOptions loaderOptions = new LoaderOptions();
    loaderOptions.setTagInspector(globalTagAllowed -> true);
    Yaml yaml = new Yaml(loaderOptions);
    Map<String, Object> graphConfigMap =
        yaml.load(new ClassPathResource(resource).getInputStream());
    Map<String, T> result = new HashMap<>();
    for (var entry : graphConfigMap.entrySet()) {
      JsonNode jsonConfig = mapper.valueToTree(entry.getValue());
      result.put(entry.getKey(), mapper.treeToValue(jsonConfig, configClass));
    }
    return result;
  }

  public List<GraphResponse> getGraphs(UUID universeUuid, List<GraphQuery> queries) {
    UniverseMetadata universeMetadata = universeMetadataService.get(universeUuid);
    UniverseDetails universeDetails = universeDetailsService.get(universeUuid);
    if (universeMetadata == null || universeDetails == null) {
      log.warn("Universe information for " + universeUuid + " is missing");
      return queries.stream()
          .map(
              q ->
                  new GraphResponse()
                      .setName(q.getName())
                      .setSuccessful(false)
                      .setErrorMessage("Universe information for " + universeUuid + " is missing"))
          .toList();
    }
    List<Pair<GraphQuery, Future<GraphResponse>>> futures = new ArrayList<>();
    List<GraphResponse> responses = new ArrayList<>();
    for (GraphQuery query : queries) {

      // Just in case it was not set in the query itself
      Map<GraphLabel, List<String>> filtersWithUniverse = new HashMap<>();
      if (query.getFilters() != null) {
        filtersWithUniverse.putAll(query.getFilters());
      }
      filtersWithUniverse.put(GraphLabel.universeUuid, ImmutableList.of(universeUuid.toString()));
      query.setFilters(filtersWithUniverse);
      boolean sourceFound = false;
      for (GraphSourceIF source : sources) {
        if (source.supportsGraph(query.getName())) {
          prepareQuery(query, source.minGraphStepSeconds(query, universeMetadata));
          preProcessFilters(universeMetadata, universeDetails, query);
          futures.add(
              new ImmutablePair<>(
                  query,
                  metricQueryExecutor.submit(
                      LogsUtil.wrapCallable(
                          () -> source.getGraph(universeMetadata, universeDetails, query)))));
          sourceFound = true;
          break;
        }
      }
      if (!sourceFound) {
        log.warn("No graph named: " + query.getName());
        responses.add(
            new GraphResponse()
                .setSuccessful(false)
                .setName(query.getName())
                .setErrorMessage("No graph named " + query.getName()));
      }
    }
    for (Pair<GraphQuery, Future<GraphResponse>> future : futures) {
      GraphQuery query = future.getKey();
      try {
        GraphResponse response = future.getValue().get();
        if (query.isFillMissingPoints()) {
          Set<Long> allTimestamps =
              response.getData().stream()
                  .flatMap(data -> data.getPoints().stream())
                  .map(GraphPoint::getX)
                  .collect(Collectors.toCollection(TreeSet::new));
          response
              .getData()
              .forEach(
                  graphData -> {
                    Map<Long, GraphPoint> pointsByTimestamp =
                        graphData.getPoints().stream()
                            .collect(Collectors.toMap(GraphPoint::getX, Function.identity()));
                    List<GraphPoint> newPoints =
                        allTimestamps.stream()
                            .map(
                                ts ->
                                    pointsByTimestamp.getOrDefault(
                                        ts, new GraphPoint(ts, Double.NaN)))
                            .toList();
                    graphData.setPoints(newPoints);
                  });
        }
        if (query.isReplaceNaN()) {
          response
              .getData()
              .forEach(
                  graphData ->
                      graphData
                          .getPoints()
                          .forEach(
                              point -> {
                                if (point.getY() != null && point.getY().isNaN()) {
                                  point.setY(0D);
                                }
                              }));
        }
        responses.add(response);
      } catch (Exception e) {
        log.warn("Failed to get graph data for query: " + query, e);
        responses.add(
            new GraphResponse()
                .setSuccessful(false)
                .setName(query.getName())
                .setErrorMessage(e.getMessage()));
      }
    }
    return responses;
  }

  private void prepareQuery(GraphQuery query, long minStep) {
    if (query.getEnd() == null) {
      query.setEnd(Instant.now());
    }
    long endSeconds = query.getEnd().getEpochSecond();
    long startSeconds = query.getStart().getEpochSecond();
    if (query.getStepSeconds() == null) {
      query.setStepSeconds((endSeconds - startSeconds) / GRAPH_POINTS_DEFAULT);
    }
    query.setStepSeconds(Math.max(minStep, query.getStepSeconds()));
    startSeconds = startSeconds - startSeconds % query.getStepSeconds();
    endSeconds = endSeconds - endSeconds % query.getStepSeconds();
    query.setStart(Instant.ofEpochSecond(startSeconds));
    query.setEnd(Instant.ofEpochSecond(endSeconds));
  }

  private void preProcessFilters(
      UniverseMetadata metadata, UniverseDetails universe, GraphQuery query) {
    // Given we have a limitation on not being able to rename the pod labels in
    // kubernetes cadvisor metrics, we try to see if the metric being queried is for
    // container or not, and use pod_name vs exported_instance accordingly.
    // Expect for container metrics, all the metrics would with node_prefix and exported_instance.
    boolean isContainerMetric = query.getName().startsWith(CONTAINER_METRIC_PREFIX);
    GraphLabel universeFilterLabel =
        isContainerMetric ? GraphLabel.namespace : GraphLabel.universeUuid;
    GraphLabel nodeFilterLabel = isContainerMetric ? GraphLabel.podName : GraphLabel.instanceName;

    List<UniverseDetails.UniverseDefinition.NodeDetails> nodesToFilter = new ArrayList<>();
    Map<GraphLabel, List<String>> filters =
        query.getFilters() != null ? query.getFilters() : new HashMap<>();
    if (filters.containsKey(GraphLabel.clusterUuid)
        || filters.containsKey(GraphLabel.regionCode)
        || filters.containsKey(GraphLabel.azCode)
        || filters.containsKey(GraphLabel.instanceName)
        || filters.containsKey(GraphLabel.instanceType)) {
      List<UUID> clusterUuids =
          filters.getOrDefault(GraphLabel.clusterUuid, Collections.emptyList()).stream()
              .map(UUID::fromString)
              .toList();
      List<String> regionCodes =
          filters.getOrDefault(GraphLabel.regionCode, Collections.emptyList());
      List<String> azCodes = filters.getOrDefault(GraphLabel.azCode, Collections.emptyList());
      List<String> instanceNames =
          filters.getOrDefault(GraphLabel.instanceName, Collections.emptyList());
      List<UniverseDetails.InstanceType> instanceTypes =
          filters.getOrDefault(GraphLabel.instanceType, Collections.emptyList()).stream()
              .map(type -> UniverseDetails.InstanceType.valueOf(type.toUpperCase()))
              .toList();
      // Need to get matching nodes
      nodesToFilter.addAll(
          universe.getUniverseDetails().getNodeDetailsSet().stream()
              .filter(
                  node -> {
                    if (CollectionUtils.isNotEmpty(instanceTypes)
                        && instanceTypes.contains(UniverseDetails.InstanceType.MASTER)
                        && !node.isMaster()) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(instanceTypes)
                        && instanceTypes.contains(UniverseDetails.InstanceType.TSERVER)
                        && !node.isTserver()) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(clusterUuids)
                        && !clusterUuids.contains(node.getPlacementUuid())) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(regionCodes)
                        && !regionCodes.contains(node.getCloudInfo().getRegion())) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(azCodes)
                        && !azCodes.contains(node.getCloudInfo().getRegion())) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(instanceNames)
                        && !instanceNames.contains(node.getNodeName())) {
                      return false;
                    }
                    return true;
                  })
              .toList());

      if (CollectionUtils.isEmpty(nodesToFilter)) {
        throw new RuntimeException(
            "No nodes found based on passed universe, "
                + "clusters, regions, availability zones and node names");
      }
    }
    // Check if it is a Kubernetes deployment.
    if (isContainerMetric) {
      if (CollectionUtils.isEmpty(nodesToFilter)) {
        nodesToFilter = new ArrayList<>(universe.getUniverseDetails().getNodeDetailsSet());
      }
      Set<String> podNames = new HashSet<>();
      Set<String> containerNames = new HashSet<>();
      Set<String> pvcNames = new HashSet<>();
      Set<String> namespaces = new HashSet<>();
      for (UniverseDetails.UniverseDefinition.NodeDetails node : nodesToFilter) {
        String podName = node.getK8sPodName();
        String namespace = node.getK8sNamespace();
        String containerName = podName.contains("yb-master") ? "yb-master" : "yb-tserver";
        String pvcName = String.format("(.*)-%s", podName);
        podNames.add(podName);
        containerNames.add(containerName);
        pvcNames.add(pvcName);
        namespaces.add(namespace);
      }
      filters.put(nodeFilterLabel, new ArrayList<>(podNames));
      filters.put(GraphLabel.containerName, new ArrayList<>(containerNames));
      filters.put(GraphLabel.pvc, new ArrayList<>(pvcNames));
      filters.put(universeFilterLabel, new ArrayList<>(namespaces));
    } else {
      if (CollectionUtils.isNotEmpty(nodesToFilter)) {
        List<String> nodeNames =
            nodesToFilter.stream()
                .map(UniverseDetails.UniverseDefinition.NodeDetails::getNodeName)
                .collect(Collectors.toList());
        filters.put(nodeFilterLabel, nodeNames);
      }

      if (DISK_USAGE_METRICS.contains(query.getName())) {
        if (DATA_DISK_USAGE_METRICS.contains(query.getName())) {
          filters.put(GraphLabel.mountPoint, metadata.getDataMountPoints());
        } else {
          List<String> allMountPoints = new ArrayList<>(metadata.getOtherMountPoints());
          allMountPoints.addAll(metadata.getDataMountPoints());
          filters.put(GraphLabel.mountPoint, allMountPoints);
        }
      }
    }
    query.setFilters(filters);
  }
}
