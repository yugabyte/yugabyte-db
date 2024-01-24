package com.yugabyte.troubleshoot.ts.models;

import java.util.List;
import java.util.Map;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphResponse {
  String name;
  boolean successful;
  String errorMessage;
  Layout layout;
  List<GraphData> data;

  @Data
  @Accessors(chain = true)
  public static class Layout {
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

  @Data
  @Accessors(chain = true)
  public static class GraphData {
    public String name;
    public String instanceName;
    public String tableName;
    public String tableId;
    public String namespaceName;
    public String namespaceId;
    public String type;
    public List<Long> x;
    public List<String> y;
    public Map<String, String> labels;
  }
}
