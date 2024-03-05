package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonFormat;
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

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant detectionTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant startTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant endTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant graphStartTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
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
