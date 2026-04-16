package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import java.util.HashMap;
import java.util.Map;
import lombok.Data;

@Data
@JsonNaming(PropertyNamingStrategies.SnakeCaseStrategy.class)
public class MetricsGraphConfig {
  private GraphLayout layout;
  private String metric;
  private String function;
  private String range;
  private Map<String, String> filters = new HashMap<>();
  private Map<String, String> excludeFilters = new HashMap<>();
  private String groupBy;
  private String operator;
}
