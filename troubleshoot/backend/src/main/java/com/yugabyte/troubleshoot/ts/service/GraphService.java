package com.yugabyte.troubleshoot.ts.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.*;
import java.io.IOException;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.time.*;
import java.time.temporal.ChronoUnit;
import java.util.*;
import java.util.concurrent.Future;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.Data;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.core.io.ClassPathResource;
import org.springframework.jdbc.core.namedparam.NamedParameterJdbcTemplate;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Service;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;

@Slf4j
@Service
public class GraphService {

  public static final Integer GRAPH_POINTS_DEFAULT = 100;
  private static final String ALIAS = "alias";
  private final ObjectMapper objectMapper;
  private final NamedParameterJdbcTemplate jdbcTemplate;
  private final UniverseMetadataService universeMetadataService;
  private final Map<String, TsStorageGraphConfig> tsStorageGraphConfigs;
  private final Map<String, MetricsGraphConfig> metricGraphConfigs;
  private final ThreadPoolTaskExecutor metricQueryExecutor;

  private final long minPgStatStatementsGraphStep;

  @SneakyThrows
  public GraphService(
      ObjectMapper objectMapper,
      NamedParameterJdbcTemplate jdbcTemplate,
      UniverseMetadataService universeMetadataService,
      ThreadPoolTaskExecutor metricQueryExecutor,
      @Value("${task.pg_stat_statements_query.period}") Duration pssPeriod) {
    this.objectMapper = objectMapper;
    this.jdbcTemplate = jdbcTemplate;
    this.universeMetadataService = universeMetadataService;
    this.metricQueryExecutor = metricQueryExecutor;
    this.tsStorageGraphConfigs =
        fillConfigMap("graphs/ts_storage_graphs.yml", TsStorageGraphConfig.class);
    this.metricGraphConfigs = fillConfigMap("graphs/metric_graphs.yml", MetricsGraphConfig.class);
    minPgStatStatementsGraphStep = pssPeriod.toSeconds() * 2;
  }

  private <T> Map<String, T> fillConfigMap(String resource, Class<T> configClass)
      throws IOException {

    LoaderOptions loaderOptions = new LoaderOptions();
    loaderOptions.setTagInspector(globalTagAllowed -> true);
    Yaml yaml = new Yaml(loaderOptions);
    Map<String, Object> tsStorageGraphConfigMap =
        yaml.load(new ClassPathResource(resource).getInputStream());
    Map<String, T> result = new HashMap<>();
    for (var entry : tsStorageGraphConfigMap.entrySet()) {
      JsonNode jsonConfig = objectMapper.valueToTree(entry.getValue());
      result.put(entry.getKey(), objectMapper.treeToValue(jsonConfig, configClass));
    }
    return result;
  }

  public List<GraphResponse> getGraphs(UUID universeUuid, List<GraphQuery> queries) {
    List<Pair<GraphQuery, Future<GraphResponse>>> futures = new ArrayList<>();
    List<GraphResponse> responses = new ArrayList<>();
    for (GraphQuery query : queries) {
      // Just in case it was not set in the query itself
      Map<GraphFilter, List<String>> filtersWithUniverse = new HashMap<>(query.getFilters());
      filtersWithUniverse.put(GraphFilter.universeUuid, ImmutableList.of(universeUuid.toString()));
      query.setFilters(filtersWithUniverse);
      if (tsStorageGraphConfigs.containsKey(query.getName())) {
        prepareQuery(query, minPgStatStatementsGraphStep);
        futures.add(
            new ImmutablePair<>(query, metricQueryExecutor.submit(() -> getTsStorageGraph(query))));
      } else if (metricGraphConfigs.containsKey(query.getName())) {
        UniverseMetadata metadata = universeMetadataService.get(universeUuid);
        prepareQuery(query, metadata.getMetricsScrapePeriodSec() * 3);
        futures.add(
            new ImmutablePair<>(query, metricQueryExecutor.submit(() -> getMetricsGraph(query))));
      } else {
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
        responses.add(future.getValue().get());
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
      query.setStepSeconds(Math.max(minStep, (endSeconds - startSeconds) / GRAPH_POINTS_DEFAULT));
    }
    startSeconds = startSeconds - startSeconds % query.getStepSeconds();
    endSeconds = endSeconds - endSeconds % query.getStepSeconds();
    query.setStart(Instant.ofEpochSecond(startSeconds));
    query.setEnd(Instant.ofEpochSecond(endSeconds));
  }

  private GraphResponse getTsStorageGraph(GraphQuery query) {
    TsStorageGraphConfig config = tsStorageGraphConfigs.get(query.getName());
    GraphResponse response = new GraphResponse();
    response.setSuccessful(true);
    response.setName(query.getName());
    response.setLayout(config.getLayout());

    // Generate SQL statement
    String sql = "SELECT ";
    sql += config.getTimestampColumn() + ", ";
    List<String> filterColumnNames =
        config.getFilterColumns().values().stream()
            .map(TsStorageGraphConfig.FilterColumn::getName)
            .toList();
    sql += StringUtils.join(filterColumnNames, ", ") + ", ";
    sql += StringUtils.join(config.getDataColumns().keySet(), ", ") + " ";
    sql += "FROM pg_stat_statements ";
    sql += "WHERE ";
    sql +=
        query.getFilters().entrySet().stream()
                .map(
                    entry -> {
                      TsStorageGraphConfig.FilterColumn filterColumn =
                          config.getFilterColumns().get(entry.getKey().name());
                      String columnName = filterColumn.getName();
                      if (entry.getValue().size() == 1) {
                        return columnName + " = :" + entry.getKey().name();
                      } else {
                        return columnName + " in (:" + entry.getKey().name() + ")";
                      }
                    })
                .collect(Collectors.joining(" AND "))
            + " ";
    sql += "AND " + config.getTimestampColumn() + " > :startTimestamp ";
    sql += "AND " + config.getTimestampColumn() + " <= :endTimestamp ";
    sql += "ORDER BY ";
    sql += StringUtils.join(filterColumnNames, ", ") + ", ";
    sql += config.getTimestampColumn();

    Map<String, Object> params = new HashMap<>();
    params.put(
        "startTimestamp",
        Timestamp.from(query.getStart().minus(query.getStepSeconds(), ChronoUnit.SECONDS)));
    params.put("endTimestamp", Timestamp.from(query.getEnd()));
    params.putAll(
        query.getFilters().entrySet().stream()
            .collect(
                Collectors.toMap(
                    e -> e.getKey().name(),
                    e -> convertParamValues(config, e.getKey().name(), e.getValue()))));

    // Get raw lines
    Map<LineKey, RawLine> rawLines =
        jdbcTemplate.query(
            sql,
            params,
            rs -> {
              Map<LineKey, RawLine> result = new HashMap<>();
              while (rs.next()) {
                LineKey key = new LineKey();
                for (var filterColumn : config.getFilterColumns().entrySet()) {
                  key.labels.put(
                      filterColumn.getKey(), readFilterValue(filterColumn.getValue(), rs));
                }
                RawLine rawLine = result.computeIfAbsent(key, k -> new RawLine());
                OffsetDateTime timestampDataTime =
                    rs.getObject(config.getTimestampColumn(), OffsetDateTime.class);
                Instant timestamp = timestampDataTime.toInstant();
                for (var dataColumn : config.getDataColumns().entrySet()) {
                  RawLineValue value = new RawLineValue();
                  value.setAlias(dataColumn.getValue().getAlias());
                  value.setValue(rs.getDouble(dataColumn.getKey()));
                  rawLine.values.add(new ImmutablePair<>(timestamp, value));
                }
              }
              return result;
            });

    if (rawLines == null) {
      return response;
    }

    // Group values, related to the same alias/timestamp together
    Map<LineKey, GroupedLine> groupedLines = new HashMap<>();
    Map<LineKey, GroupedLine> averageLines = new HashMap<>();
    for (var entry : rawLines.entrySet()) {
      RawLine rawLine = entry.getValue();
      for (Pair<Instant, RawLineValue> rawLineValuePair : rawLine.getValues()) {
        RawLineValue value = rawLineValuePair.getValue();
        LineKey key = new LineKey(entry.getKey());
        key.getLabels().put(ALIAS, value.getAlias());
        // Only node split supported for TS graphs for now.
        if (query.getSettings().getSplitType() != GraphSettings.SplitType.NODE) {
          // Will group data for all the nodes into single line.
          key.getLabels().remove(GraphFilter.instanceName.name());
        }

        // Adjust timestamp
        long pointSeconds = rawLineValuePair.getKey().getEpochSecond();
        long adjustment = pointSeconds % query.getStepSeconds();
        if (adjustment > 0) {
          pointSeconds -= adjustment;
          pointSeconds += query.getStepSeconds();
        }
        Instant aggregateTimestamp = Instant.ofEpochSecond(pointSeconds);

        GroupedLine groupedLine = groupedLines.computeIfAbsent(key, k -> new GroupedLine());
        groupedLine
            .getValues()
            .computeIfAbsent(aggregateTimestamp, k -> new ArrayList<>())
            .add(rawLineValuePair.getValue().getValue());
        groupedLine.total += rawLineValuePair.getValue().getValue();
        groupedLine.totalPoints++;

        if (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE
            && query.getSettings().isReturnAggregatedValue()) {
          // Will add one more line with average value
          LineKey averageKey = new LineKey(key);
          averageKey.getLabels().remove(GraphFilter.instanceName.name());

          GroupedLine groupedAverageLine =
              averageLines.computeIfAbsent(averageKey, k -> new GroupedLine());
          groupedAverageLine
              .getValues()
              .computeIfAbsent(aggregateTimestamp, k -> new ArrayList<>())
              .add(rawLineValuePair.getValue().getValue());
          groupedAverageLine.total += rawLineValuePair.getValue().getValue();
          groupedAverageLine.totalPoints++;
        }
      }
    }

    // Filter unneeded lines based on graph query settings
    if (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE) {
      var partitionedLines =
          groupedLines.entrySet().stream()
              .collect(
                  Collectors.groupingBy(
                      e -> {
                        LineKey groupingKey = new LineKey(e.getKey());
                        groupingKey.getLabels().remove(GraphFilter.instanceName.name());
                        return groupingKey;
                      },
                      Collectors.toList()));
      groupedLines =
          partitionedLines.values().stream()
              .flatMap(
                  s -> {
                    Comparator<? super Map.Entry<LineKey, GroupedLine>> comparator =
                        Comparator.comparing(e -> e.getValue().getAverage());
                    if (query.getSettings().getSplitMode() == GraphSettings.SplitMode.BOTTOM) {
                      comparator = comparator.reversed();
                    }
                    return s.stream()
                        .sorted(comparator)
                        .limit(
                            query.getSettings().getSplitMode() == GraphSettings.SplitMode.NONE
                                ? Long.MAX_VALUE
                                : query.getSettings().getSplitCount());
                  })
              .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));
    }
    Stream.concat(groupedLines.entrySet().stream(), averageLines.entrySet().stream())
        .forEach(
            groupedLineEntry -> {
              LineKey key = groupedLineEntry.getKey();
              GroupedLine groupedLine = groupedLineEntry.getValue();
              GraphResponse.GraphData graphData = new GraphResponse.GraphData();
              graphData.setName(key.getLabels().remove(ALIAS));
              if (key.getLabels().containsKey(GraphFilter.instanceName.name())) {
                graphData.setInstanceName(key.getLabels().remove(GraphFilter.instanceName.name()));
              }
              graphData.setLabels(key.getLabels());
              for (var valueGroup : groupedLine.values.entrySet()) {
                graphData.x.add(valueGroup.getKey().toEpochMilli());
                OptionalDouble average =
                    valueGroup.getValue().stream().mapToDouble(a -> a).average();
                if (average.isEmpty()) {
                  log.warn("No values for {} at {}", key, valueGroup.getKey());
                  continue;
                }
                graphData.y.add(String.valueOf(average.getAsDouble()));
              }
              response.getData().add(graphData);
            });
    return response;
  }

  private List<?> convertParamValues(
      TsStorageGraphConfig config, String filterName, List<String> stringValues) {
    TsStorageGraphConfig.FilterColumn column = config.getFilterColumns().get(filterName);
    return switch (column.getType()) {
      case type_text -> stringValues;
      case type_int -> stringValues.stream().map(Integer::valueOf).toList();
      case type_float -> stringValues.stream().map(Double::valueOf).toList();
      case type_bool -> stringValues.stream().map(Boolean::valueOf).toList();
      case type_uuid -> stringValues.stream().map(UUID::fromString).toList();
    };
  }

  private String readFilterValue(TsStorageGraphConfig.FilterColumn column, ResultSet rs)
      throws SQLException {
    return switch (column.getType()) {
      case type_text, type_uuid -> rs.getString(column.getName());
      case type_int -> String.valueOf(rs.getLong(column.getName()));
      case type_float -> String.valueOf(rs.getDouble(column.getName()));
      case type_bool -> String.valueOf(rs.getBoolean(column.getName()));
    };
  }

  private GraphResponse getMetricsGraph(GraphQuery query) {
    GraphResponse result = new GraphResponse();
    return result;
  }

  @Data
  private static class LineKey {
    private Map<String, String> labels = new TreeMap<>();

    private LineKey() {}

    private LineKey(LineKey other) {
      this();
      labels.putAll(other.getLabels());
    }
  }

  @Data
  private static class RawLine {
    private List<Pair<Instant, RawLineValue>> values = new ArrayList<>();
  }

  @Data
  private static class RawLineValue {
    private String alias;
    private Double value;
  }

  @Data
  private static class GroupedLine {
    private double total;
    private double totalPoints;
    private Map<Instant, List<Double>> values = new TreeMap<>();

    public double getAverage() {
      return total / totalPoints;
    }
  }
}
