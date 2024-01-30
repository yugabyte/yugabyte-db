package com.yugabyte.troubleshoot.ts.models;

import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphLayout {
  @Data
  @Accessors(chain = true)
  public static class Axis {
    private String type;
    private String ticksuffix;
    private String tickformat;
  }

  private String title;
  private Axis xaxis;
  private Axis yaxis;
}
