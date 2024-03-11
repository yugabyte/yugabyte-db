package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsQueryService;
import io.ebean.Lists;
import java.time.Duration;
import java.time.Instant;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.Pair;
import org.springframework.stereotype.Component;

@Component
public class QueryLatencyDetector extends AnomalyDetectorBase {

  private static final long MIN_ANOMALY_SIZE_MILLIS = Duration.ofMinutes(5).toMillis();
  private static final int QUERY_BATCH_SIZE = 10;

  private final PgStatStatementsQueryService pgStatStatementsQueryService;

  protected QueryLatencyDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      PgStatStatementsQueryService pgStatStatementsQueryService,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
    this.pgStatStatementsQueryService = pgStatStatementsQueryService;
  }

  public AnomalyDetectionResult findAnomalies(
      UUID universeUuid, Instant startTime, Instant endTime) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();
    List<PgStatStatementsQuery> queries =
        pgStatStatementsQueryService.listByUniverseId(universeUuid);
    Map<String, List<PgStatStatementsQuery>> queriesByDb =
        queries.stream()
            .collect(Collectors.groupingBy(q -> q.getId().getDbId(), Collectors.toList()));
    queriesByDb.forEach(
        (dbId, dbQueries) -> {
          for (List<PgStatStatementsQuery> batch : Lists.partition(dbQueries, QUERY_BATCH_SIZE)) {
            result.merge(findAnomalies(universeUuid, dbId, batch, startTime, endTime));
          }
        });
    return result;
  }

  private AnomalyDetectionResult findAnomalies(
      UUID universeUuid,
      String dbId,
      List<PgStatStatementsQuery> queries,
      Instant startTime,
      Instant endTime) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();
    GraphSettings settings = new GraphSettings();
    settings.setSplitMode(GraphSettings.SplitMode.TOP);
    settings.setSplitType(GraphSettings.SplitType.NODE);
    settings.setSplitCount(Integer.MAX_VALUE);

    Map<String, PgStatStatementsQuery> queryMap =
        queries.stream()
            .collect(
                Collectors.toMap(q -> String.valueOf(q.getId().getQueryId()), Function.identity()));
    GraphQuery graphQuery =
        new GraphQuery()
            .setName("query_latency")
            .setStart(startTime)
            .setEnd(endTime)
            .setSettings(settings)
            .setFilters(
                ImmutableMap.of(
                    GraphFilter.universeUuid, ImmutableList.of(universeUuid.toString()),
                    GraphFilter.dbId, ImmutableList.of(dbId),
                    GraphFilter.queryId, ImmutableList.copyOf(queryMap.keySet())));

    GraphResponse response =
        graphService.getGraphs(universeUuid, ImmutableList.of(graphQuery)).get(0);

    if (!response.isSuccessful()) {
      return new AnomalyDetectionResult()
          .setSuccess(false)
          .setErrorMessages(
              Collections.singleton(
                  "Failed to retrieve query latency graph: " + response.getErrorMessage()));
    }

    Map<String, List<GraphData>> queryGraphs =
        response.getData().stream()
            .collect(
                Collectors.groupingBy(
                    data -> data.getLabels().get(GraphFilter.queryId.name()), Collectors.toList()));

    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        new GraphAnomalyDetectionService.AnomalyDetectionSettings();
    long minAnomalySize = Math.max(response.getStepSeconds() * 1000, MIN_ANOMALY_SIZE_MILLIS);
    detectionSettings
        .getIncreaseDetectionSettings()
        .setWindowMinSize(minAnomalySize)
        .setWindowMaxSize(minAnomalySize * 2);

    queryGraphs.forEach(
        (queryId, data) -> {
          result.merge(
              findAnomalies(
                  detectionSettings,
                  universeUuid,
                  dbId,
                  queryId,
                  queryMap.get(queryId),
                  data,
                  startTime,
                  endTime));
        });

    return result;
  }

  private AnomalyDetectionResult findAnomalies(
      GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings,
      UUID universeUuid,
      String dbId,
      String queryId,
      PgStatStatementsQuery query,
      List<GraphData> graphDataList,
      Instant startTime,
      Instant endTime) {
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            GraphAnomaly.GraphAnomalyType.INCREASE, graphDataList, detectionSettings);

    List<GraphAnomaly> mergedAnomalies = anomalyDetectionService.mergeAnomalies(anomalies);

    AnomalyDetectionResult result = new AnomalyDetectionResult();
    mergedAnomalies.forEach(
        graphAnomaly -> {
          AnomalyMetadata metadata =
              metadataProvider.getMetadata(AnomalyMetadata.AnomalyType.SQL_QUERY_LATENCY_INCREASE);
          AnomalyMetadata.AnomalyMetadataBuilder metadataBuilder = metadata.toBuilder();
          metadataBuilder.mainGraphs(
              metadata.getMainGraphs().stream()
                  .map(t -> fillGraphMetadata(t, universeUuid, dbId, queryId))
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
                                                                          t,
                                                                          universeUuid,
                                                                          dbId,
                                                                          queryId))
                                                              .toList())
                                                      .build())
                                      .toList())
                              .build())
                  .toList());

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
          Anomaly anomaly =
              Anomaly.builder()
                  .uuid(UUID.randomUUID())
                  .universeUuid(universeUuid)
                  .affectedNodes(
                      graphAnomaly.getLabels().get(GraphFilter.instanceName.name()).stream()
                          .map(nodeName -> Anomaly.NodeInfo.builder().name(nodeName).build())
                          .toList())
                  .metadata(metadataBuilder.build())
                  .summary(
                      "Latencies increased for query '"
                          + query.getQuery()
                          + "' in database '"
                          + query.getDbName()
                          + "'")
                  .detectionTime(Instant.now())
                  .startTime(anomalyStartTime)
                  .endTime(anomalyEndTime)
                  .graphStartTime(graphStartEndTime.getLeft())
                  .graphEndTime(graphStartEndTime.getRight())
                  .build();

          result.getAnomalies().add(anomaly);
        });
    return result;
  }

  private GraphMetadata fillGraphMetadata(
      GraphMetadata template, UUID universeUuid, String dbId, String queryId) {
    GraphMetadata.GraphMetadataBuilder metadataBuilder = template.toBuilder();
    Map<GraphFilter, List<String>> filters = new HashMap<>(template.getFilters());
    if (template.getFilters().containsKey(GraphFilter.universeUuid)) {
      filters.put(GraphFilter.universeUuid, ImmutableList.of(universeUuid.toString()));
    }
    if (template.getFilters().containsKey(GraphFilter.dbId)) {
      filters.put(GraphFilter.dbId, ImmutableList.of(dbId));
    }
    if (template.getFilters().containsKey(GraphFilter.queryId)) {
      filters.put(GraphFilter.queryId, ImmutableList.of(queryId));
    }
    return metadataBuilder.filters(filters).build();
  }
}
