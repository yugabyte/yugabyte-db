package com.yugabyte.troubleshoot.ts.models;

import lombok.Data;

@Data
class GraphSettings {
  private SplitMode splitMode = SplitMode.NONE;
  private SplitType splitType = SplitType.NONE;
  private int splitCount;
  private boolean returnAggregatedValue = false;
  private Aggregation aggregatedValueFunction = Aggregation.AVG;

  public enum SplitMode {
    NONE,
    TOP,
    BOTTOM
  }

  public enum SplitType {
    NONE,
    NODE,
    TABLE,
    NAMESPACE
  }

  public enum Aggregation {
    DEFAULT,
    MIN,
    MAX,
    AVG,
    SUM
  }
}
