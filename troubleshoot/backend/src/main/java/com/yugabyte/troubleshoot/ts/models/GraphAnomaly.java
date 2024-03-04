package com.yugabyte.troubleshoot.ts.models;

import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphAnomaly {
  String graphName;
  GraphAnomalyType type;
  Long startTime;
  Long endTime;

  public enum GraphAnomalyType {
    INCREASE,
    UNEVEN_DISTRIBUTION
  }
}
