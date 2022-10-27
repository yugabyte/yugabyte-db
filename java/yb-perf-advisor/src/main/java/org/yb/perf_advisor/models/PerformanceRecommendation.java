// Copyright (c) Yugabyte, Inc.
package org.yb.perf_advisor.models;

import io.ebean.Model;
import io.ebean.annotation.DbJsonB;
import io.ebean.annotation.EnumValue;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import javax.persistence.Table;
import java.time.OffsetDateTime;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Entity
@Getter
@Setter
@Table
public class PerformanceRecommendation extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(PerformanceRecommendation.class);

  @Id
  public UUID id;

  public UUID universeId;

  @Enumerated(EnumType.STRING)
  public RecommendationType recommendationType;

  public String observation;

  public String recommendation;

  @Enumerated(EnumType.STRING)
  public EntityType entityType;

  public String entityNames;

  @DbJsonB
  public Map<String, Object> recommendationInfo;

  @Enumerated(EnumType.STRING)
  public RecommendationState recommendationState;

  @Enumerated(EnumType.STRING)
  public RecommendationPriority recommendationPriority;

  public OffsetDateTime recommendationTimestamp;

  @OneToMany(mappedBy = "performanceRecommendation")
  private List<StateChangeAuditInfo> stateChangeAuditInfoList;

  public enum RecommendationType {
    QUERY_LOAD_SKEW,
    UNUSED_INDEX,
    RANGE_SHARDING,
    CONNECTION_SKEW,
    CPU_SKEW,
    CPU_USAGE
  }

  public enum RecommendationState {
    OPEN,
    HIDDEN,
    RESOLVED
  }

  public enum RecommendationPriority {
    HIGH,
    MEDIUM,
    LOW
  }

  public enum EntityType {
    UNIVERSE,
    NODE,
    TABLE,
    DATABASE,
    INDEX;
  }
}
