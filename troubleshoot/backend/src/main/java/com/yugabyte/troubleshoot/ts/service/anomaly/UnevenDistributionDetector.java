package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.time.Duration;
import java.time.Instant;
import java.util.*;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.Pair;

public abstract class UnevenDistributionDetector extends AnomalyDetectorBase {

  private static final long MIN_ANOMALY_SIZE_MILLIS = Duration.ofMinutes(5).toMillis();

  protected UnevenDistributionDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  protected abstract double getMinAnomalyValue();

  protected long getMinAnomalySizeMillis() {
    return MIN_ANOMALY_SIZE_MILLIS;
  }

  protected abstract String getGraphName();

  protected abstract AnomalyMetadata.AnomalyType getAnomalyType();

  protected abstract Anomaly.AnomalyBuilder fillAnomaly(
      Anomaly.AnomalyBuilder builder, String affectedNodes, GraphAnomaly graphAnomaly);

  public AnomalyDetectionResult findAnomalies(
      UUID universeUuid, Instant startTime, Instant endTime) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();
    GraphSettings settings = new GraphSettings();
    settings.setSplitMode(GraphSettings.SplitMode.TOP);
    settings.setSplitType(GraphSettings.SplitType.NODE);
    settings.setSplitCount(Integer.MAX_VALUE);

    GraphQuery graphQuery =
        new GraphQuery()
            .setName(getGraphName())
            .setStart(startTime)
            .setEnd(endTime)
            .setSettings(settings)
            .setFilters(
                ImmutableMap.of(
                    GraphFilter.universeUuid, ImmutableList.of(universeUuid.toString())));

    GraphResponse response =
        graphService.getGraphs(universeUuid, ImmutableList.of(graphQuery)).get(0);

    if (!response.isSuccessful()) {
      return new AnomalyDetectionResult()
          .setSuccess(false)
          .setErrorMessages(
              Collections.singleton(
                  "Failed to retrieve "
                      + getGraphName()
                      + " graph: "
                      + response.getErrorMessage()));
    }

    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        new GraphAnomalyDetectionService.AnomalyDetectionSettings()
            .setMinimalAnomalyValue(getMinAnomalyValue());
    long minAnomalySize = Math.max(response.getStepSeconds() * 1000, getMinAnomalySizeMillis());
    detectionSettings
        .getIncreaseDetectionSettings()
        .setWindowMinSize(minAnomalySize)
        .setWindowMaxSize(minAnomalySize * 2);

    Map<String, List<GraphData>> graphsByLineName =
        response.getData().stream()
            .collect(Collectors.groupingBy(GraphData::getName, Collectors.toList()));

    List<GraphAnomaly> anomalies = new ArrayList<>();
    graphsByLineName.forEach(
        (mode, graphs) ->
            anomalies.addAll(
                anomalyDetectionService.getAnomalies(
                    GraphAnomaly.GraphAnomalyType.UNEVEN_DISTRIBUTION, graphs, detectionSettings)));

    List<GraphAnomaly> mergedAnomalies = anomalyDetectionService.mergeAnomalies(anomalies);

    mergedAnomalies.forEach(
        graphAnomaly -> {
          AnomalyMetadata metadata = metadataProvider.getMetadata(getAnomalyType());
          AnomalyMetadata.AnomalyMetadataBuilder metadataBuilder = metadata.toBuilder();
          metadataBuilder.mainGraphs(
              metadata.getMainGraphs().stream()
                  .map(t -> fillGraphMetadata(t, universeUuid))
                  .toList());
          metadataBuilder.rcaGuidelines(
              metadata.getRcaGuidelines().stream()
                  .map(
                      rcaGuideline ->
                          rcaGuideline.toBuilder()
                              .troubleshootingRecommendations(
                                  rcaGuideline.getTroubleshootingRecommendations().stream()
                                      .map(
                                          recommendation ->
                                              CollectionUtils.isEmpty(
                                                      recommendation.getSupportingGraphs())
                                                  ? recommendation
                                                  : recommendation.toBuilder()
                                                      .supportingGraphs(
                                                          recommendation
                                                              .getSupportingGraphs()
                                                              .stream()
                                                              .map(
                                                                  t ->
                                                                      fillGraphMetadata(
                                                                          t, universeUuid))
                                                              .toList())
                                                      .build())
                                      .toList())
                              .build())
                  .toList());

          List<Anomaly.NodeInfo> affectedNodes =
              graphAnomaly.getLabels().get(GraphFilter.instanceName.name()).stream()
                  .map(nodeName -> Anomaly.NodeInfo.builder().name(nodeName).build())
                  .toList();
          String addectedNodesStr =
              affectedNodes.stream()
                  .map(Anomaly.NodeInfo::getName)
                  .map(n -> "'" + n + "'")
                  .collect(Collectors.joining(", "));

          Instant anomalyStartTime =
              graphAnomaly.getStartTime() != null
                  ? Instant.ofEpochMilli(graphAnomaly.getStartTime())
                  : null;
          Instant anomalyEndTime =
              graphAnomaly.getEndTime() != null
                  ? Instant.ofEpochMilli(graphAnomaly.getEndTime())
                  : null;
          Pair<Instant, Instant> graphStartEndTime =
              calculateGraphStartEndTime(startTime, endTime, anomalyStartTime, anomalyEndTime);
          Anomaly.AnomalyBuilder anomalyBuilder =
              Anomaly.builder()
                  .uuid(UUID.randomUUID())
                  .universeUuid(universeUuid)
                  .affectedNodes(affectedNodes)
                  .metadata(metadataBuilder.build())
                  .detectionTime(Instant.now())
                  .startTime(anomalyStartTime)
                  .endTime(anomalyEndTime)
                  .graphStartTime(graphStartEndTime.getLeft())
                  .graphEndTime(graphStartEndTime.getRight());
          fillAnomaly(anomalyBuilder, addectedNodesStr, graphAnomaly);

          result.getAnomalies().add(anomalyBuilder.build());
        });

    return result;
  }

  private GraphMetadata fillGraphMetadata(GraphMetadata template, UUID universeUuid) {
    GraphMetadata.GraphMetadataBuilder metadataBuilder = template.toBuilder();
    Map<GraphFilter, List<String>> filters = new HashMap<>(template.getFilters());
    if (template.getFilters().containsKey(GraphFilter.universeUuid)) {
      filters.put(GraphFilter.universeUuid, ImmutableList.of(universeUuid.toString()));
    }
    return metadataBuilder.filters(filters).build();
  }
}
