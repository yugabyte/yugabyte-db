package com.yugabyte.troubleshoot.ts.models;

import java.util.ArrayList;
import java.util.List;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class GraphResponse {
  private String name;
  private boolean successful;
  private String errorMessage;
  private GraphLayout layout;
  private long stepSeconds;
  private List<GraphData> data = new ArrayList<>();
}
