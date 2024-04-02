package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import lombok.Data;

@Data
public class Anomaly {
  private UUID uuid;
  @JsonUnwrapped private AnomalyMetadata metadata;
  private UUID universeUuid;
  private List<NodeInfo> affectedNodes;
  private List<TableInfo> affectedTables;
  private String summary;
  private Instant detectionTime;
  private Instant startTime;
  private Instant endTime;
  private Instant graphStartTime;
  private Instant graphEndTime;

  @Data
  public static class NodeInfo {
    private String name;
    private UUID uuid;
  }

  @Data
  public static class TableInfo {
    private String databaseName;
    private String tableName;
    private String tableId;
  }
}
