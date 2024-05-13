package com.yugabyte.troubleshoot.ts.service.anomaly;

import static com.yugabyte.troubleshoot.ts.MetricsUtil.buildSummary;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import io.prometheus.client.Summary;
import java.time.Instant;
import java.util.*;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

public abstract class AnomalyDetectorBase implements AnomalyDetector {

  private static String ANOMALY_TYPE = "anomaly_type";

  static final Summary DETECTION_FULL_TIME =
      buildSummary(
          "ts_anomaly_detection_full_time_millis", "Graph data retrieval time", ANOMALY_TYPE);

  protected final GraphService graphService;
  protected final AnomalyMetadataProvider metadataProvider;

  protected final GraphAnomalyDetectionService anomalyDetectionService;

  protected AnomalyDetectorBase(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    this.graphService = graphService;
    this.metadataProvider = metadataProvider;
    this.anomalyDetectionService = anomalyDetectionService;
  }

  @Override
  public AnomalyDetectionResult findAnomalies(AnomalyDetectionContext context) {
    long startTime = System.currentTimeMillis();
    AnomalyDetectionResult result = findAnomaliesInternal(context);
    DETECTION_FULL_TIME
        .labels(getAnomalyType().name())
        .observe(System.currentTimeMillis() - startTime);
    return result;
  }

  protected abstract AnomalyDetectionResult findAnomaliesInternal(AnomalyDetectionContext context);

  protected Pair<Instant, Instant> calculateGraphStartEndTime(
      AnomalyDetectionContext context, Instant anomalyStartTime, Instant anomalyEndTime) {
    long startTime = context.getStartTime().getEpochSecond();
    long endTime = context.getEndTime().getEpochSecond();

    long effectiveAnomalyStartTime =
        anomalyStartTime != null ? anomalyStartTime.getEpochSecond() : startTime;
    long effectiveAnomalyEndTime =
        anomalyEndTime != null ? anomalyEndTime.getEpochSecond() : endTime;
    long anomalyMiddle = (effectiveAnomalyStartTime + effectiveAnomalyEndTime) / 2;
    long anomalySize = effectiveAnomalyEndTime - effectiveAnomalyStartTime;
    long anomalySizeBasedStartTime = anomalyMiddle - anomalySize * 2;
    long anomalySizeBasedEndTime = anomalyMiddle + anomalySize * 2;

    // 20 points before and after the anomaly should be good enough
    long stepBasedStartTime = anomalyMiddle - context.getStepSeconds() * 20;
    long stepBasedEndTime = anomalyMiddle + context.getStepSeconds() * 20;

    long desiredStartTime = Math.min(anomalySizeBasedStartTime, stepBasedStartTime);
    long desiredEndTime = Math.max(anomalySizeBasedEndTime, stepBasedEndTime);

    if (desiredStartTime > startTime) {
      startTime = desiredStartTime;
    }
    if (desiredEndTime < endTime) {
      endTime = desiredEndTime;
    }

    return ImmutablePair.of(Instant.ofEpochSecond(startTime), Instant.ofEpochSecond(endTime));
  }

  protected abstract AnomalyMetadata.AnomalyType getAnomalyType();

  protected abstract Anomaly.AnomalyBuilder fillAnomaly(
      Anomaly.AnomalyBuilder builder,
      AnomalyDetectionContext context,
      String affectedNodes,
      GraphAnomaly graphAnomaly);

  protected GraphResponse queryNodeMetric(
      AnomalyDetectionContext context, String graphName, AnomalyDetectionResult result) {
    GraphSettings settings = new GraphSettings();
    settings.setSplitMode(GraphSettings.SplitMode.TOP);
    settings.setSplitType(GraphSettings.SplitType.NODE);
    settings.setSplitCount(Integer.MAX_VALUE);

    GraphQuery graphQuery =
        new GraphQuery()
            .setName(graphName)
            .setStart(context.getStartTime())
            .setEnd(context.getEndTime())
            .setStepSeconds(context.getStepSeconds())
            .setSettings(settings)
            .setReplaceNaN(false)
            .setFilters(
                ImmutableMap.of(
                    GraphFilter.universeUuid,
                    ImmutableList.of(context.getUniverseUuid().toString())));

    GraphResponse response =
        graphService.getGraphs(context.getUniverseUuid(), ImmutableList.of(graphQuery)).get(0);

    if (!response.isSuccessful()) {
      result
          .setSuccess(false)
          .setErrorMessages(
              Collections.singleton(
                  "Failed to retrieve " + graphName + " graph: " + response.getErrorMessage()));
    }
    return response;
  }

  private GraphMetadata fillGraphMetadata(
      GraphMetadata template, GraphAnomaly graphAnomaly, AnomalyDetectionContext context) {
    GraphMetadata.GraphMetadataBuilder metadataBuilder = template.toBuilder();
    Map<GraphFilter, List<String>> filters = new HashMap<>(template.getFilters());
    for (GraphFilter filterKey : template.getFilters().keySet()) {
      if (graphAnomaly.getLabels().containsKey(filterKey.name())) {
        filters.put(filterKey, new ArrayList<>(graphAnomaly.getLabels().get(filterKey.name())));
      }
    }
    if (template.getFilters().containsKey(GraphFilter.universeUuid)) {
      filters.put(GraphFilter.universeUuid, ImmutableList.of(context.getUniverseUuid().toString()));
    }
    return metadataBuilder.filters(filters).build();
  }

  protected List<List<GraphData>> groupGraphLines(List<GraphData> lines) {
    return new ArrayList<>(
        lines.stream()
            .collect(Collectors.groupingBy(this::graphLinesGroupBy, Collectors.toList()))
            .values());
  }

  protected List<List<GraphAnomaly>> groupGraphAnomalies(List<GraphAnomaly> anomalies) {
    return new ArrayList<>(
        anomalies.stream()
            .collect(Collectors.groupingBy(this::anomaliesGroupBy, Collectors.toList()))
            .values());
  }

  protected String graphLinesGroupBy(GraphData graphData) {
    return graphData.getName();
  }

  protected String anomaliesGroupBy(GraphAnomaly anomaly) {
    return StringUtils.EMPTY;
  }

  protected void groupAndCreateAnomalies(
      AnomalyDetectionContext context,
      List<GraphAnomaly> graphAnomalies,
      AnomalyDetectionResult result) {
    List<List<GraphAnomaly>> groupedAnomalies = groupGraphAnomalies(graphAnomalies);

    List<List<GraphAnomaly>> mergedAnomalies =
        groupedAnomalies.stream().map(anomalyDetectionService::mergeAnomalies).toList();

    mergedAnomalies.forEach(anomalyGroup -> createAnomalies(context, anomalyGroup, result));
  }

  protected void createAnomalies(
      AnomalyDetectionContext context,
      List<GraphAnomaly> graphAnomalies,
      AnomalyDetectionResult result) {
    graphAnomalies.forEach(
        graphAnomaly -> {
          AnomalyMetadata metadata = metadataProvider.getMetadata(getAnomalyType());
          AnomalyMetadata.AnomalyMetadataBuilder metadataBuilder = metadata.toBuilder();
          metadataBuilder.mainGraphs(
              metadata.getMainGraphs().stream()
                  .map(t -> fillGraphMetadata(t, graphAnomaly, context))
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
                                                                          t, graphAnomaly, context))
                                                              .toList())
                                                      .build())
                                      .toList())
                              .build())
                  .toList());

          List<Anomaly.NodeInfo> affectedNodes =
              graphAnomaly.getLabels().get(GraphFilter.instanceName.name()).stream()
                  .filter(Objects::nonNull)
                  .map(nodeName -> Anomaly.NodeInfo.builder().name(nodeName).build())
                  .toList();
          String addectedNodesStr = StringUtils.EMPTY;
          if (!affectedNodes.isEmpty()) {
            addectedNodesStr =
                affectedNodes.stream()
                    .map(Anomaly.NodeInfo::getName)
                    .map(n -> "'" + n + "'")
                    .collect(Collectors.joining(", "));
          }

          Instant anomalyStartTime =
              graphAnomaly.getStartTime() != null
                  ? Instant.ofEpochMilli(graphAnomaly.getStartTime())
                  : null;
          Instant anomalyEndTime =
              graphAnomaly.getEndTime() != null
                  ? Instant.ofEpochMilli(graphAnomaly.getEndTime())
                  : null;
          Pair<Instant, Instant> graphStartEndTime =
              calculateGraphStartEndTime(context, anomalyStartTime, anomalyEndTime);
          Anomaly.AnomalyBuilder anomalyBuilder =
              Anomaly.builder()
                  .uuid(UUID.randomUUID())
                  .universeUuid(context.getUniverseUuid())
                  .affectedNodes(affectedNodes)
                  .metadata(metadataBuilder.build())
                  .detectionTime(Instant.now())
                  .startTime(anomalyStartTime)
                  .endTime(anomalyEndTime)
                  .graphStepSeconds(context.getStepSeconds())
                  .graphStartTime(graphStartEndTime.getLeft())
                  .graphEndTime(graphStartEndTime.getRight());
          fillAnomaly(anomalyBuilder, context, addectedNodesStr, graphAnomaly);

          result.getAnomalies().add(anomalyBuilder.build());
        });
  }

  protected abstract RuntimeConfigKey getMinAnomalyDurationKey();
}
