package com.yugabyte.troubleshoot.ts.service;

import static com.yugabyte.troubleshoot.ts.service.GraphService.fillConfigMap;

import com.fasterxml.jackson.databind.ObjectMapper;
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

  private final long minPgStatStatementsGraphStep;

  @SneakyThrows
  public TsStorageGraphService(
      ObjectMapper objectMapper,
      NamedParameterJdbcTemplate jdbcTemplate,
      @Value("${task.pg_stat_statements_query.period}") Duration pssPeriod) {
    this.jdbcTemplate = jdbcTemplate;
    this.tsStorageGraphConfigs =
        fillConfigMap(objectMapper, "graphs/ts_storage_graphs.yml", TsStorageGraphConfig.class);
    minPgStatStatementsGraphStep = pssPeriod.toSeconds() * 2;
  }

  public boolean supportsGraph(String name) {
    return tsStorageGraphConfigs.containsKey(name);
  }

  public long minGraphStepSeconds(UniverseMetadata universeMetadata) {
    return minPgStatStatementsGraphStep;
  }

  public GraphResponse getGraph(
      UniverseMetadata universeMetadata, UniverseDetails universeDetails, GraphQuery query) {
    TsStorageGraphConfig config = tsStorageGraphConfigs.get(query.getName());
    GraphResponse response = new GraphResponse();
    response.setSuccessful(true);
    response.setName(query.getName());
    response.setLayout(config.getLayout());
    response.setStepSeconds(query.getStepSeconds());

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
              GraphData graphData = new GraphData();
              graphData.setName(key.getLabels().remove(ALIAS));
              if (key.getLabels().containsKey(GraphFilter.instanceName.name())) {
                graphData.setInstanceName(key.getLabels().remove(GraphFilter.instanceName.name()));
              }
              graphData.setLabels(key.getLabels());
              for (var valueGroup : groupedLine.values.entrySet()) {
                OptionalDouble average =
                    valueGroup.getValue().stream().mapToDouble(a -> a).average();
                if (average.isEmpty()) {
                  log.warn("No values for {} at {}", key, valueGroup.getKey());
                  continue;
                }
                graphData
                    .getPoints()
                    .add(
                        new GraphPoint()
                            .setX(valueGroup.getKey().toEpochMilli())
                            .setY(average.getAsDouble()));
              }
              response.getData().add(graphData);
            });
    Comparator<GraphData> graphDataComparator =
        Comparator.comparing(GraphData::getInstanceNameOrEmpty)
            .thenComparing(GraphData::getNameOrEmpty);
    response.setData(response.getData().stream().sorted(graphDataComparator).toList());
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
