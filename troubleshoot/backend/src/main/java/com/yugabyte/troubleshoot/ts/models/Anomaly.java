package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;

@Value
@Builder(toBuilder = true)
@Jacksonized
public class Anomaly {
  UUID uuid;
  @JsonUnwrapped AnomalyMetadata metadata;
  UUID universeUuid;
  List<NodeInfo> affectedNodes;
  List<TableInfo> affectedTables;
  String summary;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  Instant detectionTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  Instant startTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  Instant endTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  Instant graphStartTime;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  Instant graphEndTime;

  Long graphStepSeconds;

  @Value
  @Builder(toBuilder = true)
  @Jacksonized
  public static class NodeInfo {
    String name;
    UUID uuid;
  }

  @Value
  @Builder(toBuilder = true)
  @Jacksonized
  public static class TableInfo {
    String databaseName;
    String tableName;
    String tableId;
  }
}
