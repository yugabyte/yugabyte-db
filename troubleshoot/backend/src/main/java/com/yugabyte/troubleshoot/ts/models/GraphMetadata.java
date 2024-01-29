package com.yugabyte.troubleshoot.ts.models;

import java.util.List;
import java.util.Map;
import lombok.Data;

@Data
public class GraphMetadata {
  private String name;
  private Double threshold;
  private Map<GraphFilter, List<String>> filters;
}
