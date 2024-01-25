package com.yugabyte.troubleshoot.ts.models;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import lombok.Data;

@Data
public class GraphQuery {
  private Instant start;
  private Instant end;
  private String name;
  private Map<String, List<String>> filters;
  private GraphSettings settings;
}
