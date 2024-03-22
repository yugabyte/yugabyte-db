package com.yugabyte.troubleshoot.ts.models;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphQuery {
  private Instant start;
  private Instant end;
  private Long stepSeconds;
  private String name;
  private Map<GraphFilter, List<String>> filters;
  private GraphSettings settings;
  private boolean replaceNaN = true;
}
