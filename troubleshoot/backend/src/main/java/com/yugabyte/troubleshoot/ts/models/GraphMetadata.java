package com.yugabyte.troubleshoot.ts.models;

import java.util.List;
import java.util.Map;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;

@Value
@Builder(toBuilder = true)
@Jacksonized
public class GraphMetadata {
  String name;
  Double threshold;
  Map<GraphFilter, List<String>> filters;
}
