package com.yugabyte.troubleshoot.ts.service;

import static com.yugabyte.troubleshoot.ts.MetricsUtil.*;
import static com.yugabyte.troubleshoot.ts.service.GraphService.*;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.Streams;
import com.yugabyte.troubleshoot.ts.models.*;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.time.Duration;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.temporal.ChronoUnit;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.DoubleStream;
import java.util.stream.Stream;
import lombok.Data;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.namedparam.NamedParameterJdbcTemplate;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class TsStorageGraphService implements GraphSourceIF {

  public static final String ALIAS = "alias";
  private final NamedParameterJdbcTemplate jdbcTemplate;
  private final Map<String, TsStorageGraphConfig> tsStorageGraphConfigs;

  private final RuntimeConfigService runtimeConfigService;

  private final long minPgStatStatementsGraphStep;

  @SneakyThrows
  public TsStorageGraphService(
      ObjectMapper objectMapper,
      NamedParameterJdbcTemplate jdbcTemplate,
      RuntimeConfigService runtimeConfigService,
      @Value("${task.pg_stat_statements_query.period}") Duration pssPeriod) {
    this.jdbcTemplate = jdbcTemplate;
    this.tsStorageGraphConfigs =
        fillConfigMap(objectMapper, "graphs/ts_storage_graphs.yml", TsStorageGraphConfig.class);
    this.runtimeConfigService = runtimeConfigService;
    minPgStatStatementsGraphStep = pssPeriod.toSeconds() * 2;
  }

  public boolean supportsGraph(String name) {
    return tsStorageGraphConfigs.containsKey(name);
  }

  public long minGraphStepSeconds(GraphQuery query, UniverseMetadata universeMetadata) {
    TsStorageGraphConfig config = tsStorageGraphConfigs.get(query.getName());
    switch (config.getTable()) {
      case "pg_stat_statements":
        return minPgStatStatementsGraphStep;
      case "active_session_history":
        return runtimeConfigService
            .getUniverseConfig(universeMetadata)
            .getDuration(RuntimeConfigKey.ASH_AGGREGATION_PERIOD)
            .toSeconds();
      default:
        throw new IllegalArgumentException("Table " + config.getTable() + " is not supported");
    }
  }

  public GraphResponse getGraph(
      UniverseMetadata universeMetadata, UniverseDetails universeDetails, GraphQuery query) {
    Long startTime = System.currentTimeMillis();

    TsStorageGraphConfig config = tsStorageGraphConfigs.get(query.getName());
    GraphResponse response = new GraphResponse();
    response.setSuccessful(true);
    response.setName(query.getName());
    response.setLayout(config.getLayout());
    response.setStepSeconds(query.getStepSeconds());

    Set<String> groupByLabels = new LinkedHashSet<>();
    if (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE) {
      // Will group data for all the nodes into single line.
      groupByLabels.add(GraphLabel.instanceName.name());
    }
    String groupByColumn = null;
    if (query.getGroupBy() != null) {
      groupByLabels.addAll(
          config.getFilterColumns().entrySet().stream()
              .filter(e -> e.getKey().equals(query.getGroupBy().name()))
              .flatMap(
                  e ->
                      Streams.concat(
                          Stream.of(e.getKey()), e.getValue().getAssumesGroupBy().stream()))
              .collect(Collectors.toSet()));
      groupByColumn = query.getGroupBy().name();
    } else {
      groupByLabels.addAll(
          config.getFilterColumns().entrySet().stream()
              .filter(e -> e.getValue().isDefaultGroupBy())
              .flatMap(
                  e ->
                      Streams.concat(
                          Stream.of(e.getKey()), e.getValue().getAssumesGroupBy().stream()))
              .collect(Collectors.toSet()));
      groupByColumn =
          config.getFilterColumns().entrySet().stream()
              .filter(e -> e.getValue().isDefaultGroupBy())
              .map(Map.Entry::getKey)
              .findFirst()
              .orElse(null);
    }
    if (response.getLayout().getMetadata() != null && groupByColumn != null) {
      response.getLayout().getMetadata().setCurrentGroupBy(GraphLabel.valueOf(groupByColumn));
    }
    Set<String> filterByLabels =
        query.getFilters().keySet().stream().map(GraphLabel::name).collect(Collectors.toSet());
    Map<String, TsStorageGraphConfig.FilterColumn> columnsToRead =
        config.getFilterColumns().entrySet().stream()
            .filter(
                entry ->
                    groupByLabels.contains(entry.getKey())
                        || filterByLabels.contains(entry.getKey())
                        || (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE
                            && entry.getKey().equals(GraphLabel.instanceName.name())))
            .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

    // Generate SQL statement
    String sql = "SELECT ";
    sql += config.getTimestampColumn() + ", ";
    List<String> toReadColumnNames =
        columnsToRead.values().stream().map(TsStorageGraphConfig.FilterColumn::getName).toList();
    sql += StringUtils.join(toReadColumnNames, ", ") + ", ";
    sql += StringUtils.join(config.getDataColumns().keySet(), ", ") + " ";
    sql += "FROM " + config.getTable() + " ";
    sql += "WHERE ";
    sql +=
        query.getFilters().entrySet().stream()
                .filter(e -> config.getFilterColumns().containsKey(e.getKey().name()))
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
    if (CollectionUtils.isNotEmpty(toReadColumnNames)) {
      sql += StringUtils.join(toReadColumnNames, ", ") + ", ";
    }
    sql += config.getTimestampColumn();

    Map<String, Object> params = new HashMap<>();
    params.put(
        "startTimestamp",
        Timestamp.from(query.getStart().minus(query.getStepSeconds(), ChronoUnit.SECONDS)));
    params.put("endTimestamp", Timestamp.from(query.getEnd()));
    params.putAll(
        query.getFilters().entrySet().stream()
            .filter(e -> config.getFilterColumns().containsKey(e.getKey().name()))
            .collect(
                Collectors.toMap(
                    e -> e.getKey().name(),
                    e -> convertParamValues(config, e.getKey().name(), e.getValue()))));

    long queryStartTime = System.currentTimeMillis();
    // Get raw lines
    Map<LineKey, RawLine> rawLines =
        jdbcTemplate.query(
            sql,
            params,
            rs -> {
              Map<LineKey, RawLine> result = new HashMap<>();
              while (rs.next()) {
                LineKey key = new LineKey();
                for (var filterColumn : columnsToRead.entrySet()) {
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
    DATA_RETRIEVAL_TIME
        .labels(query.getName())
        .observe(System.currentTimeMillis() - queryStartTime);

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
        LineKey key = new LineKey();
        key.getLabels()
            .putAll(
                entry.getKey().getLabels().entrySet().stream()
                    .filter(e -> groupByLabels.contains(e.getKey()))
                    .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue)));
        key.getLabels().put(ALIAS, value.getAlias());

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
          averageKey.getLabels().remove(GraphLabel.instanceName.name());

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
                        groupingKey.getLabels().remove(GraphLabel.instanceName.name());
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
    Map<String, TsStorageGraphConfig.DataColumn> dataColumnByAlias =
        config.getDataColumns().entrySet().stream()
            .collect(Collectors.toMap(e -> e.getValue().getAlias(), Map.Entry::getValue));
    String nameColumn = groupByColumn;
    Stream.concat(groupedLines.entrySet().stream(), averageLines.entrySet().stream())
        .forEach(
            groupedLineEntry -> {
              LineKey key = groupedLineEntry.getKey();
              GroupedLine groupedLine = groupedLineEntry.getValue();
              GraphData graphData = new GraphData();
              String alias = key.getLabels().remove(ALIAS);
              if (nameColumn != null && config.getDataColumns().size() == 1) {
                graphData.setName(key.getLabels().remove(nameColumn));
              } else {
                graphData.setName(alias);
              }
              if (key.getLabels().containsKey(GraphLabel.instanceName.name())) {
                graphData.setInstanceName(key.getLabels().remove(GraphLabel.instanceName.name()));
              }
              if (key.getLabels().containsKey(GraphLabel.waitEventComponent.name())) {
                graphData.setWaitEventComponent(
                    key.getLabels().remove(GraphLabel.waitEventComponent.name()));
              }
              if (key.getLabels().containsKey(GraphLabel.waitEventClass.name())) {
                graphData.setWaitEventClass(
                    key.getLabels().remove(GraphLabel.waitEventClass.name()));
              }
              if (key.getLabels().containsKey(GraphLabel.waitEvent.name())) {
                graphData.setWaitEvent(key.getLabels().remove(GraphLabel.waitEvent.name()));
              }

              graphData.setLabels(key.getLabels());
              for (var valueGroup : groupedLine.values.entrySet()) {
                double aggregated = Double.NaN;
                DoubleStream valuesStream = valueGroup.getValue().stream().mapToDouble(a -> a);
                TsStorageGraphConfig.DataColumn dataColumn = dataColumnByAlias.get(alias);
                switch (dataColumn.getAggregation()) {
                  case avg -> aggregated = valuesStream.average().getAsDouble();
                  case sum -> aggregated = valuesStream.sum();
                  case max -> aggregated = valuesStream.max().getAsDouble();
                  case min -> aggregated = valuesStream.min().getAsDouble();
                }
                graphData
                    .getPoints()
                    .add(
                        new GraphPoint().setX(valueGroup.getKey().toEpochMilli()).setY(aggregated));
              }
              response.getData().add(graphData);
            });
    Comparator<GraphData> graphDataComparator =
        Comparator.comparing(GraphData::getInstanceNameOrEmpty)
            .thenComparing(GraphData::getNameOrEmpty)
            .thenComparing(GraphData::getWaitEventClassOrEmpty)
            .thenComparing(GraphData::getWaitEventOrEmpty);
    response.setData(response.getData().stream().sorted(graphDataComparator).toList());

    QUERY_TIME
        .labels(RESULT_SUCCESS, query.getName())
        .observe(System.currentTimeMillis() - startTime);
    return response;
  }

  private List<?> convertParamValues(
      TsStorageGraphConfig config, String filterName, List<String> stringValues) {
    TsStorageGraphConfig.FilterColumn column = config.getFilterColumns().get(filterName);
    return switch (column.getType()) {
      case type_text -> stringValues;
      case type_int -> stringValues.stream().map(Long::valueOf).toList();
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
