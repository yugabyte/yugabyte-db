package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import lombok.Data;

@Data
@JsonNaming(PropertyNamingStrategies.SnakeCaseStrategy.class)
public class TsStorageGraphConfig {
  private GraphLayout layout;
  private String table;
  private String timestampColumn;
  private Map<String, FilterColumn> filterColumns;
  private Map<String, DataColumn> dataColumns;
  private String additionalFilter;

  @Data
  public static class DataColumn {
    private String alias;
    private AggregationFunction aggregation = AggregationFunction.avg;
  }

  public enum AggregationFunction {
    avg,
    sum,
    min,
    max
  }

  @Data
  public static class FilterColumn {
    private String name;
    private String defaultValue;
    private FilterColumnType type = FilterColumnType.type_text;
    private boolean neverRead = false;
    private boolean alwaysGroupBy = false;
    private boolean defaultGroupBy = false;
    private List<String> assumesGroupBy = Collections.emptyList();
  }

  public enum FilterColumnType {
    type_text,
    type_int,
    type_float,
    type_bool,
    type_uuid
  }
}
