package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsQueryService;
import java.util.*;
import org.springframework.stereotype.Component;

@Component
public class QueryLatencyDetector extends QueryAnomalyDetector {

  protected QueryLatencyDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      PgStatStatementsQueryService pgStatStatementsQueryService,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, pgStatStatementsQueryService, anomalyDetectionService);
  }

  @Override
  protected String getGraphName() {
    return "query_latency";
  }

  @Override
  protected Integer getBatchSize(AnomalyDetectionContext context) {
    return context.getConfig().getInt(RuntimeConfigKey.QUERY_LATENCY_BATCH_SIZE);
  }

  @Override
  protected GraphAnomalyDetectionService.AnomalyDetectionSettings createSettings(
      GraphResponse response, AnomalyDetectionContext context) {
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
    return detectionSettings;
  }

  @Override
  protected AnomalyDetectionResult findAnomalies(
      AnomalyDetectionContext context,
      GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings,
      PgStatStatementsQuery query,
      List<GraphData> graphDataList) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    List<GraphData> graphsWithoutMaxLatency =
        graphDataList.stream().filter(gd -> !gd.getName().equals("Max")).toList();
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            GraphAnomaly.GraphAnomalyType.INCREASE, graphsWithoutMaxLatency, detectionSettings);

    AnomalyDetectionContext updatedContext =
        context.toBuilder()
            .customContext(new QueryAnomalyDetectionContext().setQuery(query))
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
    QueryAnomalyDetectionContext customContext =
        (QueryAnomalyDetectionContext) context.getCustomContext();
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
}
