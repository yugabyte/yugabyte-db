package com.yugabyte.troubleshoot.ts.models;

import java.util.List;
import java.util.UUID;
import lombok.Data;

@Data
public class AnomalyMetadata {
  private UUID metadataUuid;
  private AnomalyCategory category;
  private AnomalyType type;
  private String title;
  private List<GraphMetadata> mainGraphs;
  private List<GraphMetadata> supportingGraphs;
  private GraphSettings defaultSettings;
  private List<RCAGuideline> rcaGuidelines;

  public enum AnomalyCategory {
    SQL,
    APP,
    NODE,
    INFRA,
    DB
  }

  public enum AnomalyType {
    SQL_QUERY_LATENCY_INCREASE,
    HOT_NODE_CPU,
    HOT_NODE_QUERIES,
    HOT_NODE_DATA,
    SLOW_DISKS
  }

  @Data
  public static class RCAGuideline {
    private String possibleCause;
    private String possibleCauseDescription;
    private List<String> troubleshootingRecommendations;
  }

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
