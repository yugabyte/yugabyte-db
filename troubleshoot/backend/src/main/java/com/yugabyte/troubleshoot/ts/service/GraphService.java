package com.yugabyte.troubleshoot.ts.service;

import static com.yugabyte.troubleshoot.ts.MetricsUtil.LABEL_RESULT;
import static com.yugabyte.troubleshoot.ts.MetricsUtil.buildSummary;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.logs.LogsUtil;
import com.yugabyte.troubleshoot.ts.models.*;
import io.prometheus.client.Summary;
import java.io.IOException;
import java.time.*;
import java.util.*;
import java.util.concurrent.Future;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
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
      Map<GraphFilter, List<String>> filtersWithUniverse = new HashMap<>();
      if (query.getFilters() != null) {
        filtersWithUniverse.putAll(query.getFilters());
      }
      filtersWithUniverse.put(GraphFilter.universeUuid, ImmutableList.of(universeUuid.toString()));
      query.setFilters(filtersWithUniverse);
      boolean sourceFound = false;
      for (GraphSourceIF source : sources) {
        if (source.supportsGraph(query.getName())) {
          prepareQuery(query, source.minGraphStepSeconds(universeMetadata));
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
}
