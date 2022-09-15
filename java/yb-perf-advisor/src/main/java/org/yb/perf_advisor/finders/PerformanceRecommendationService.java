package org.yb.perf_advisor.finders;

import io.ebean.EbeanServer;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.*;

import java.time.OffsetDateTime;
import java.util.Map;
import java.util.UUID;

public class PerformanceRecommendationService {

  EbeanServer ebeanServer;

  public PerformanceRecommendationService(EbeanServer ebeanServer) {
    this.ebeanServer = ebeanServer;
  }

  public PerformanceRecommendation create(PerformanceRecommendation performanceRecommendation){
    ebeanServer.save(performanceRecommendation);
    return performanceRecommendation;
  }

  public PerformanceRecommendation create(UUID universeId, String recommendation, String observation, RecommendationType recommendationType,  EntityType entityType, String entityNames, Map<String, Object> recommendationInfo, RecommendationState recommendationState, RecommendationPriority recommendationPriority){
    PerformanceRecommendation performanceRecommendation = new PerformanceRecommendation();
    performanceRecommendation.universeId=universeId;
    performanceRecommendation.recommendationType= recommendationType;
    performanceRecommendation.recommendation=recommendation;
    performanceRecommendation.entityType= entityType;
    performanceRecommendation.entityNames=entityNames;
    performanceRecommendation.recommendationInfo=recommendationInfo;
    performanceRecommendation.recommendationState= recommendationState;
    performanceRecommendation.recommendationPriority= recommendationPriority;
    performanceRecommendation.observation=observation;
    performanceRecommendation.recommendationTimestamp= OffsetDateTime.now();
    return create(performanceRecommendation);
  }

  public PerformanceRecommendation update(PerformanceRecommendation performanceRecommendation){
    ebeanServer.update(performanceRecommendation);
    return performanceRecommendation;
  }

  public PerformanceRecommendation get(UUID id){
    return ebeanServer.find(PerformanceRecommendation.class).where().eq("id", id).findOne();
  }
}
