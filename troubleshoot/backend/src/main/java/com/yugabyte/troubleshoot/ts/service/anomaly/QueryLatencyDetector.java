package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsQueryService;
import io.ebean.Lists;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.experimental.Accessors;
import org.springframework.stereotype.Component;

@Component
public class QueryLatencyDetector extends AnomalyDetectorBase {

  private final PgStatStatementsQueryService pgStatStatementsQueryService;

  protected QueryLatencyDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      PgStatStatementsQueryService pgStatStatementsQueryService,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
    this.pgStatStatementsQueryService = pgStatStatementsQueryService;
  }

  protected AnomalyDetectionResult findAnomaliesInternal(AnomalyDetectionContext context) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();
    List<PgStatStatementsQuery> queries =
        pgStatStatementsQueryService.listByUniverseId(context.getUniverseUuid());
    Map<String, List<PgStatStatementsQuery>> queriesByDb =
        queries.stream()
            .collect(Collectors.groupingBy(q -> q.getId().getDbId(), Collectors.toList()));
    queriesByDb.forEach(
        (dbId, dbQueries) -> {
          for (List<PgStatStatementsQuery> batch :
              Lists.partition(
                  dbQueries,
                  context.getConfig().getInt(RuntimeConfigKey.QUERY_LATENCY_BATCH_SIZE))) {
            result.merge(findAnomalies(context, dbId, batch));
          }
        });
    return result;
  }

  private AnomalyDetectionResult findAnomalies(
      AnomalyDetectionContext context, String dbId, List<PgStatStatementsQuery> queries) {
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
            .setStart(context.getStartTime())
            .setEnd(context.getEndTime())
            .setStepSeconds(context.getStepSeconds())
            .setSettings(settings)
            .setReplaceNaN(false)
            .setFilters(
                ImmutableMap.of(
                    GraphFilter.universeUuid,
                        ImmutableList.of(context.getUniverseUuid().toString()),
                    GraphFilter.dbId, ImmutableList.of(dbId),
                    GraphFilter.queryId, ImmutableList.copyOf(queryMap.keySet())));

    GraphResponse response =
        graphService.getGraphs(context.getUniverseUuid(), ImmutableList.of(graphQuery)).get(0);

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

    long minAnomalyDurationMillis =
        Math.max(
            response.getStepSeconds() * 1000,
            context.getConfig().getDuration(getMinAnomalyDurationKey()).toMillis());
    double minAnomalyValue =
        context.getConfig().getDouble(RuntimeConfigKey.QUERY_LATENCY_MIN_ANOMALY_VALUE);
    double baselinePointsRation =
        context.getConfig().getDouble(RuntimeConfigKey.QUERY_LATENCY_BASELINE_POINTS_RATIO);
    double thresholdRatio =
        context.getConfig().getDouble(RuntimeConfigKey.QUERY_LATENCY_THRESHOLD_RATIO);
    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        new GraphAnomalyDetectionService.AnomalyDetectionSettings()
            .setMinimalAnomalyDurationMillis(minAnomalyDurationMillis)
            .setMinimalAnomalyValue(minAnomalyValue);
    detectionSettings
        .getIncreaseDetectionSettings()
        .setBaselinePointsRatio(baselinePointsRation)
        .setThresholdRatio(thresholdRatio)
        .setWindowMinSize(minAnomalyDurationMillis)
        .setWindowMaxSize(minAnomalyDurationMillis * 2);

    AnomalyDetectionContext contextWithUpdatedStep =
        context.toBuilder().stepSeconds(response.getStepSeconds()).build();

    queryGraphs.forEach(
        (queryId, data) -> {
          result.merge(
              findAnomalies(
                  contextWithUpdatedStep, detectionSettings, queryMap.get(queryId), data));
        });

    return result;
  }

  private AnomalyDetectionResult findAnomalies(
      AnomalyDetectionContext context,
      GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings,
      PgStatStatementsQuery query,
      List<GraphData> graphDataList) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            GraphAnomaly.GraphAnomalyType.INCREASE, graphDataList, detectionSettings);

    AnomalyDetectionContext updatedContext =
        context.toBuilder()
            .customContext(new QueryLatencyDetectionContext().setQuery(query))
            .build();

    groupAndCreateAnomalies(updatedContext, anomalies, result);

    return result;
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.SQL_QUERY_LATENCY_INCREASE;
  }

  @Override
  protected Anomaly.AnomalyBuilder fillAnomaly(
      Anomaly.AnomalyBuilder builder,
      AnomalyDetectionContext context,
      String affectedNodes,
      GraphAnomaly graphAnomaly) {
    QueryLatencyDetectionContext customContext =
        (QueryLatencyDetectionContext) context.getCustomContext();
    builder.summary(
        "Latencies increased for query '"
            + customContext.getQuery().getQuery()
            + "' in database '"
            + customContext.getQuery().getDbName()
            + "'");
    return builder;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyDurationKey() {
    return RuntimeConfigKey.QUERY_LATENCY_MIN_ANOMALY_DURATION;
  }

  @Data
  @Accessors(chain = true)
  private static class QueryLatencyDetectionContext {
    private PgStatStatementsQuery query;
  }
}
