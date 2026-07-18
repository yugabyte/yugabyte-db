package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsQueryService;
import io.ebean.Lists;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.experimental.Accessors;

public abstract class QueryAnomalyDetector extends AnomalyDetectorBase {

  private final PgStatStatementsQueryService pgStatStatementsQueryService;

  protected QueryAnomalyDetector(
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
            .filter(
                query ->
                    query.getLastActive() != null
                        && query.getLastActive().isAfter(context.getStartTime()))
            .collect(Collectors.groupingBy(q -> q.getId().getDbId(), Collectors.toList()));
    queriesByDb.forEach(
        (dbId, dbQueries) -> {
          for (List<PgStatStatementsQuery> batch :
              Lists.partition(dbQueries, getBatchSize(context))) {
            result.merge(findAnomalies(context, dbId, batch));
          }
        });
    return result;
  }

  protected abstract String getGraphName();

  protected abstract Integer getBatchSize(AnomalyDetectionContext context);

  protected GraphQuery createQuery(
      AnomalyDetectionContext context, String dbId, Map<String, PgStatStatementsQuery> queryMap) {
    GraphSettings settings = new GraphSettings();
    settings.setSplitMode(GraphSettings.SplitMode.TOP);
    settings.setSplitType(GraphSettings.SplitType.NODE);
    settings.setSplitCount(Integer.MAX_VALUE);

    return new GraphQuery()
        .setName(getGraphName())
        .setStart(context.getStartTime())
        .setEnd(context.getEndTime())
        .setStepSeconds(context.getStepSeconds())
        .setSettings(settings)
        .setReplaceNaN(false)
        .setFillMissingPoints(false)
        .setGroupBy(ImmutableList.of(GraphLabel.queryId))
        .setFilters(
            ImmutableMap.of(
                GraphLabel.universeUuid, ImmutableList.of(context.getUniverseUuid().toString()),
                GraphLabel.dbId, ImmutableList.of(dbId),
                GraphLabel.queryId, ImmutableList.copyOf(queryMap.keySet())));
  }

  private AnomalyDetectionResult findAnomalies(
      AnomalyDetectionContext context, String dbId, List<PgStatStatementsQuery> queries) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    Map<String, PgStatStatementsQuery> queryMap =
        queries.stream()
            .collect(
                Collectors.toMap(q -> String.valueOf(q.getId().getQueryId()), Function.identity()));
    GraphQuery graphQuery = createQuery(context, dbId, queryMap);

    GraphResponse response =
        graphService.getGraphs(context.getUniverseUuid(), ImmutableList.of(graphQuery)).get(0);

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

    Map<String, List<GraphData>> queryGraphs =
        response.getData().stream()
            .collect(
                Collectors.groupingBy(
                    data -> data.getLabels().get(GraphLabel.queryId.name()), Collectors.toList()));

    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        createSettings(response, context);

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

  protected abstract GraphAnomalyDetectionService.AnomalyDetectionSettings createSettings(
      GraphResponse response, AnomalyDetectionContext context);

  protected abstract AnomalyDetectionResult findAnomalies(
      AnomalyDetectionContext context,
      GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings,
      PgStatStatementsQuery query,
      List<GraphData> graphDataList);

  @Data
  @Accessors(chain = true)
  protected static class QueryAnomalyDetectionContext {
    private PgStatStatementsQuery query;
  }
}
