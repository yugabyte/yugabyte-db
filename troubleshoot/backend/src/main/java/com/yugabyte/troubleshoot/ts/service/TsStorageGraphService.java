package com.yugabyte.troubleshoot.ts.service;

import static com.yugabyte.troubleshoot.ts.MetricsUtil.*;
import static com.yugabyte.troubleshoot.ts.service.GraphService.*;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
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
import java.util.stream.Stream;
import lombok.Data;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
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
    response.setLayout(config.getLayout().toBuilder().build());
    response.setStepSeconds(query.getStepSeconds());

    Set<String> groupByLabels = new LinkedHashSet<>();
    if (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE) {
      // Will group data for all the nodes into single line.
      groupByLabels.add(GraphLabel.instanceName.name());
    }
    List<String> groupByColumns;
    if (CollectionUtils.isNotEmpty(query.getGroupBy())) {
      groupByColumns =
          query.getGroupBy().stream().map(GraphLabel::name).collect(Collectors.toList());
      groupByLabels.addAll(
          config.getFilterColumns().entrySet().stream()
              .filter(e -> groupByColumns.contains(e.getKey()))
              .flatMap(
                  e ->
                      Streams.concat(
                          Stream.of(e.getKey()), e.getValue().getAssumesGroupBy().stream()))
              .collect(Collectors.toSet()));
    } else {
      groupByLabels.addAll(
          config.getFilterColumns().entrySet().stream()
              .filter(e -> e.getValue().isDefaultGroupBy())
              .flatMap(
                  e ->
                      Streams.concat(
                          Stream.of(e.getKey()), e.getValue().getAssumesGroupBy().stream()))
              .collect(Collectors.toSet()));
      groupByColumns =
          config.getFilterColumns().entrySet().stream()
              .filter(e -> e.getValue().isDefaultGroupBy())
              .map(Map.Entry::getKey)
              .collect(Collectors.toList());
    }
    if (response.getLayout().getMetadata() != null && CollectionUtils.isNotEmpty(groupByColumns)) {
      response
          .getLayout()
          .setMetadata(
              response.getLayout().getMetadata().toBuilder()
                  .currentGroupBy(
                      groupByColumns.stream().map(GraphLabel::valueOf).collect(Collectors.toList()))
                  .build());
    }
    Map<String, TsStorageGraphConfig.FilterColumn> columnsToRead =
        config.getFilterColumns().entrySet().stream()
            .filter(
                entry ->
                    groupByLabels.contains(entry.getKey())
                        || (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE
                            && entry.getKey().equals(GraphLabel.instanceName.name())))
            .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

    config.getFilterColumns().entrySet().stream()
        .filter(entry -> StringUtils.isNotEmpty(entry.getValue().getDefaultValue()))
        .filter(entry -> !query.getFilters().containsKey(GraphLabel.valueOf(entry.getKey())))
        .forEach(
            entry ->
                query
                    .getFilters()
                    .put(
                        GraphLabel.valueOf(entry.getKey()),
                        ImmutableList.of(entry.getValue().getDefaultValue())));

    // Generate SQL statement
    String sql = "SELECT ";
    sql +=
        "date_bin('"
            + query.getStepSeconds()
            + " seconds', "
            + config.getTimestampColumn()
            + ", to_timestamp("
            + query.getStart().getEpochSecond()
            + ")) as bucket, ";
    List<String> toReadColumnNames =
        columnsToRead.values().stream().map(TsStorageGraphConfig.FilterColumn::getName).toList();
    if (CollectionUtils.isNotEmpty(toReadColumnNames)) {
      sql += StringUtils.join(toReadColumnNames, ", ") + ", ";
    }
    sql +=
        StringUtils.join(
                config.getDataColumns().entrySet().stream()
                    .map(
                        e ->
                            e.getValue().getAggregation().name()
                                + "("
                                + e.getKey()
                                + ") as "
                                + e.getKey())
                    .toList(),
                ", ")
            + " ";
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
    if (StringUtils.isNotEmpty(config.getAdditionalFilter())) {
      sql += "AND " + config.getAdditionalFilter() + " ";
    }
    sql += "AND " + config.getTimestampColumn() + " > :startTimestamp ";
    sql += "AND " + config.getTimestampColumn() + " <= :endTimestamp ";
    sql += "GROUP BY ";
    if (CollectionUtils.isNotEmpty(toReadColumnNames)) {
      sql += StringUtils.join(toReadColumnNames, ", ") + ", ";
    }
    sql += "bucket ";
    sql += "ORDER BY ";
    if (CollectionUtils.isNotEmpty(toReadColumnNames)) {
      sql += StringUtils.join(toReadColumnNames, ", ") + ", ";
    }
    sql += "bucket";

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
    String nameColumn = groupByColumns.stream().findFirst().orElse(null);
    // Get raw lines
    Map<LineKey, GraphData> graphDatas =
        jdbcTemplate.query(
            sql,
            params,
            rs -> {
              Map<LineKey, GraphData> result = new HashMap<>();
              while (rs.next()) {
                for (var dataColumn : config.getDataColumns().entrySet()) {
                  GraphData newData = new GraphData();
                  String lineName = dataColumn.getValue().getAlias();
                  Map<String, String> labels = new HashMap<>();
                  LineKey lineKey = new LineKey();
                  lineKey.getLabels().put(ALIAS, dataColumn.getValue().getAlias());
                  for (var filterColumn : columnsToRead.entrySet()) {
                    String filterColumnName = filterColumn.getKey();
                    String filterColumnValue = readFilterValue(filterColumn.getValue(), rs);
                    lineKey.getLabels().put(filterColumnName, filterColumnValue);
                    if (nameColumn != null
                        && nameColumn.equals(filterColumnName)
                        && config.getDataColumns().size() == 1) {
                      lineName = filterColumnValue;
                    }
                    if (filterColumnName.equals(GraphLabel.instanceName.name())) {
                      newData.setInstanceName(filterColumnValue);
                      continue;
                    }
                    if (filterColumnName.equals(GraphLabel.waitEventComponent.name())) {
                      newData.setWaitEventComponent(filterColumnValue);
                      continue;
                    }
                    if (filterColumnName.equals(GraphLabel.waitEventClass.name())) {
                      newData.setWaitEventClass(filterColumnValue);
                      continue;
                    }
                    if (filterColumnName.equals(GraphLabel.waitEventType.name())) {
                      newData.setWaitEventType(filterColumnValue);
                      continue;
                    }
                    if (filterColumnName.equals(GraphLabel.waitEvent.name())) {
                      newData.setWaitEvent(filterColumnValue);
                      continue;
                    }
                    labels.put(filterColumnName, filterColumnValue);
                  }
                  newData.setName(lineName);
                  newData.setLabels(labels);
                  GraphData graphData = result.computeIfAbsent(lineKey, k -> newData);
                  OffsetDateTime timestampDataTime = rs.getObject("bucket", OffsetDateTime.class);
                  Instant timestamp = timestampDataTime.toInstant();
                  Double dataColumnValue = rs.getDouble(dataColumn.getKey());
                  graphData
                      .getPoints()
                      .add(new GraphPoint().setX(timestamp.toEpochMilli()).setY(dataColumnValue));
                  graphData.appendToTotal(dataColumnValue);
                }
              }
              return result;
            });
    DATA_RETRIEVAL_TIME
        .labels(query.getName())
        .observe(System.currentTimeMillis() - queryStartTime);

    if (graphDatas == null) {
      return response;
    }

    // Calculate average lines, if needed
    Map<LineKey, GraphData> averageDatas = new HashMap<>();
    if (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE
        && query.getSettings().isReturnAggregatedValue()) {
      Set<String> nodes = new HashSet<>();
      Map<LineKey, List<GraphData>> nodeLines =
          graphDatas.entrySet().stream()
              .collect(
                  Collectors.groupingBy(
                      e -> {
                        LineKey key = new LineKey(e.getKey());
                        nodes.add(key.getLabels().remove(GraphLabel.instanceName.name()));
                        return key;
                      },
                      Collectors.mapping(Map.Entry::getValue, Collectors.toList())));
      // Will add one more line with average value
      for (var lines : nodeLines.entrySet()) {
        List<GraphData> nodeGraphs = lines.getValue();
        GraphData averageLine =
            nodeGraphs.get(0).toBuilder()
                .instanceName(null)
                .points(new ArrayList<>())
                .total(0D)
                .build();
        Map<Long, List<Double>> timestampToValues =
            nodeGraphs.stream()
                .flatMap(d -> d.getPoints().stream())
                .collect(
                    Collectors.groupingBy(
                        GraphPoint::getX,
                        TreeMap::new,
                        Collectors.mapping(GraphPoint::getY, Collectors.toList())));
        timestampToValues.forEach(
            (x, yList) -> {
              Double value =
                  yList.stream()
                      .filter(v -> !v.isNaN())
                      .reduce(Double.NaN, (a, b) -> a.isNaN() ? b : a + b);
              if (!value.isNaN()) {
                averageLine.appendToTotal(value);
                value = value / nodes.size();
              }
              averageLine.getPoints().add(new GraphPoint(x, value));
              averageDatas.put(lines.getKey(), averageLine);
            });
      }
    }

    // Filter unneeded lines based on graph query settings
    if (query.getSettings().getSplitType() == GraphSettings.SplitType.NODE) {
      var partitionedLines =
          graphDatas.entrySet().stream()
              .collect(
                  Collectors.groupingBy(
                      e -> {
                        LineKey groupingKey = new LineKey(e.getKey());
                        groupingKey.getLabels().remove(GraphLabel.instanceName.name());
                        return groupingKey;
                      },
                      Collectors.toList()));
      graphDatas =
          partitionedLines.values().stream()
              .flatMap(
                  s -> {
                    Comparator<? super Map.Entry<LineKey, GraphData>> comparator =
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
    response.getData().addAll(graphDatas.values());
    response.getData().addAll(averageDatas.values());
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
}
