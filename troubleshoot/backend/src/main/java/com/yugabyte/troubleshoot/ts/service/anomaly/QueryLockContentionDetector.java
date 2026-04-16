package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsQueryService;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.springframework.stereotype.Component;

@Component
public class QueryLockContentionDetector extends QueryAnomalyDetector {

  private static final String CONFLICT_RESOLUTION_PREFIX = "ConflictResolution";

  protected QueryLockContentionDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      PgStatStatementsQueryService pgStatStatementsQueryService,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, pgStatStatementsQueryService, anomalyDetectionService);
  }

  @Override
  protected String getGraphName() {
    return "active_session_history_tserver";
  }

  @Override
  protected Integer getBatchSize(AnomalyDetectionContext context) {
    return context.getConfig().getInt(RuntimeConfigKey.QUERY_LOCK_CONTENTION_BATCH_SIZE);
  }

  @Override
  protected GraphQuery createQuery(
      AnomalyDetectionContext context, String dbId, Map<String, PgStatStatementsQuery> queryMap) {
    GraphQuery result = super.createQuery(context, dbId, queryMap);
    result.setGroupBy(ImmutableList.of(GraphLabel.waitEvent, GraphLabel.queryId));
    result.setFillMissingPoints(true);
    result.setReplaceNaN(true);
    return result;
  }

  @Override
  protected GraphAnomalyDetectionService.AnomalyDetectionSettings createSettings(
      GraphResponse response, AnomalyDetectionContext context) {
    long minAnomalyDurationMillis =
        Math.max(
            response.getStepSeconds() * 1000,
            context.getConfig().getDuration(getMinAnomalyDurationKey()).toMillis());
    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        new GraphAnomalyDetectionService.AnomalyDetectionSettings()
            .setMinimalAnomalyDurationMillis(minAnomalyDurationMillis);
    detectionSettings
        .getIncreaseDetectionSettings()
        .setWindowMinSize(minAnomalyDurationMillis)
        .setWindowMaxSize(minAnomalyDurationMillis * 2);
    detectionSettings
        .getThresholdExceedSettings()
        .setThreshold(
            context.getConfig().getDouble(RuntimeConfigKey.QUERY_LOCK_CONTENTION_THRESHOLD_RATIO));

    return detectionSettings;
  }

  protected AnomalyDetectionResult findAnomalies(
      AnomalyDetectionContext context,
      GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings,
      PgStatStatementsQuery query,
      List<GraphData> graphDataList) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    Map<String, List<GraphData>> graphsByNode =
        graphDataList.stream()
            .collect(Collectors.groupingBy(GraphData::getInstanceName, Collectors.toList()));

    List<GraphData> lockEventsRatioGraph =
        graphsByNode.entrySet().stream()
            .map(
                entry -> {
                  GraphData converted = new GraphData();
                  converted.setInstanceName(entry.getKey());
                  converted.setLabels(
                      ImmutableMap.of(
                          GraphLabel.dbId.name(), query.getId().getDbId(),
                          GraphLabel.queryId.name(), String.valueOf(query.getId().getQueryId())));
                  List<GraphData> eventLines = entry.getValue();
                  GraphData firstLine = eventLines.get(0);
                  for (int i = 0; i < firstLine.getPoints().size(); i++) {
                    double allEvents = 0;
                    double lockEvents = 0;
                    for (GraphData eventLine : eventLines) {
                      allEvents += eventLine.getPoints().get(i).getY();
                      if (eventLine.getName().startsWith(CONFLICT_RESOLUTION_PREFIX)) {
                        lockEvents += eventLine.getPoints().get(i).getY();
                      }
                    }
                    converted
                        .getPoints()
                        .add(
                            new GraphPoint()
                                .setX(firstLine.getPoints().get(i).getX())
                                .setY(lockEvents / allEvents));
                  }
                  return converted;
                })
            .toList();

    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            GraphAnomaly.GraphAnomalyType.EXCEED_THRESHOLD,
            lockEventsRatioGraph,
            detectionSettings);

    AnomalyDetectionContext updatedContext =
        context.toBuilder()
            .customContext(new QueryAnomalyDetectionContext().setQuery(query))
            .build();

    groupAndCreateAnomalies(updatedContext, anomalies, result);

    return result;
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.SQL_QUERY_LOCK_CONTENTION;
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
        "Lock contention detected for query '"
            + customContext.getQuery().getQuery()
            + "' in database '"
            + customContext.getQuery().getDbName()
            + "'");
    return builder;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyDurationKey() {
    return RuntimeConfigKey.QUERY_LOCK_CONTENTION_MIN_ANOMALY_DURATION;
  }
}
