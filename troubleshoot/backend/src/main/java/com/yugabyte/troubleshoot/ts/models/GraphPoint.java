package com.yugabyte.troubleshoot.ts.models;

import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphPoint {
  private Long x;
  private Double y;

  public GraphPoint setY(Double y) {
    // For now we replace NaN with 0, similar to YBA.
    // As effectively we have NaN for latencies in case no responses were made.
    // It makes sense to show 0 as latency in this case.
    if (y != null && y.isNaN()) {
      this.y = 0D;
    } else {
      this.y = y;
    }
    return this;
  }
}
