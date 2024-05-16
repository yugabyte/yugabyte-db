package com.yugabyte.troubleshoot.ts.models;

import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphPoint {
  private Long x;
  private Double y;
}
