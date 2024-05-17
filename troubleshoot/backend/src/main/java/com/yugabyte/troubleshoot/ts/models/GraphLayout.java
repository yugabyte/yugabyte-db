package com.yugabyte.troubleshoot.ts.models;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphLayout {
  @Data
  @Accessors(chain = true)
  public static class Axis {
    private String type;
    private Map<String, String> alias = new HashMap<>();
    private String ticksuffix;
    private String tickformat;
  }

  @Data
  @Accessors(chain = true)
  public static class Metadata {
    private List<GroupByLabel> supportedGroupBy;
    private GraphLabel currentGroupBy;
  }

  @Data
  @Accessors(chain = true)
  public static class GroupByLabel {
    private GraphLabel label;
    private String name;
  }

  private String title;
  private GraphType type = GraphType.COMMON;
  private Axis xaxis;
  private Axis yaxis;
  private Metadata metadata;
}
