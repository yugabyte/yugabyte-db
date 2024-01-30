package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
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

  @Data
  public static class DataColumn {
    private String alias;
  }

  @Data
  public static class FilterColumn {
    private String name;
    private FilterColumnType type = FilterColumnType.type_text;
  }

  public enum FilterColumnType {
    type_text,
    type_int,
    type_float,
    type_bool,
    type_uuid
  }
}
