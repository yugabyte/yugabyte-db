package com.yugabyte.troubleshoot.ts.models;

import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;

@Value
@Builder(toBuilder = true)
@Jacksonized
public class AnomalyMetadata {
  UUID metadataUuid;
  AnomalyCategory category;
  AnomalyType type;
  String title;
  List<GraphMetadata> mainGraphs;
  GraphSettings defaultSettings;
  List<RCAGuideline> rcaGuidelines;

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
    HOT_NODE_READS_WRITES,
    HOT_NODE_YSQL_QUERIES,
    HOT_NODE_DATA,
    SLOW_DISKS
  }

  @Value
  @Builder(toBuilder = true)
  @Jacksonized
  public static class RCAGuideline {
    String possibleCause;
    String possibleCauseDescription;
    List<Recommendation> troubleshootingRecommendations;
  }

  @Value
  @Builder(toBuilder = true)
  @Jacksonized
  public static class Recommendation {
    String recommendation;
    List<GraphMetadata> supportingGraphs;
  }
}
