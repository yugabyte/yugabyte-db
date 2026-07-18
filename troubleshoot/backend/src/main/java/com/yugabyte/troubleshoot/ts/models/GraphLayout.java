package com.yugabyte.troubleshoot.ts.models;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@Builder(toBuilder = true)
@NoArgsConstructor
@AllArgsConstructor
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
  @Builder(toBuilder = true)
  @NoArgsConstructor
  @AllArgsConstructor
  public static class Metadata {
    private List<GroupByLabel> supportedGroupBy;
    private List<GraphLabel> currentGroupBy;
  }

  @Data
  @Accessors(chain = true)
  public static class GroupByLabel {
    private GraphLabel label;
    private String name;
  }

  private String title;
  @Builder.Default private GraphType type = GraphType.COMMON;
  private Axis xaxis;
  private Axis yaxis;
  private Metadata metadata;
}
