package org.yb.perf_advisor.finders;

import io.ebean.DataIntegrityException;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.yb.perf_advisor.common.MockServer;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.*;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.params.provider.Arguments.arguments;

import org.yb.util.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class PerformanceRecommendationServiceTest extends MockServer {

  private PerformanceRecommendationService performanceRecommendationService;

  @BeforeEach
  public void setUp() {
    performanceRecommendationService = new PerformanceRecommendationService(ebeanServer);
  }

  @Test
  public void testCreateAndGetPerformanceRecommendation() {
    Map<String, Object> recInfo = new HashMap<>();
    recInfo.put("sample-key", "sample-value");

    PerformanceRecommendation performanceRecommendation = performanceRecommendationService.create(UUID.randomUUID(),
      "recommendation",
      "observation",
      RecommendationType.QUERY_LOAD_SKEW,
      EntityType.NODE,
      "node-1, node-2",
      recInfo,
      RecommendationState.OPEN,
      RecommendationPriority.MEDIUM
    );

    PerformanceRecommendation recommendation = performanceRecommendationService.get(performanceRecommendation.getId());
    assertEquals(performanceRecommendation.getId(), recommendation.getId());
    assertEquals(performanceRecommendation.getRecommendation(), recommendation.getRecommendation());
    assertEquals(performanceRecommendation.getObservation(), recommendation.getObservation());
    assertEquals(performanceRecommendation.getRecommendationType(), recommendation.getRecommendationType());
    assertEquals(performanceRecommendation.getEntityNames(), recommendation.getEntityNames());
    assertEquals(performanceRecommendation.getEntityType(), recommendation.getEntityType());
    assertEquals(performanceRecommendation.getRecommendationState(), recommendation.getRecommendationState());
    assertEquals(performanceRecommendation.getRecommendationInfo(), recommendation.getRecommendationInfo());
    assertEquals(performanceRecommendation.getRecommendationPriority(), recommendation.getRecommendationPriority());
  }

  @Test
  public void testCreateAndUpdatePerformanceRecommendation() {
    Map<String, Object> recInfo = new HashMap<>();
    recInfo.put("sample-key", "sample-value");

    PerformanceRecommendation performanceRecommendation = performanceRecommendationService.create(UUID.randomUUID(),
      "recommendation",
      "observation",
      RecommendationType.QUERY_LOAD_SKEW,
      EntityType.NODE,
      "node-1, node-2",
      recInfo,
      RecommendationState.OPEN,
      RecommendationPriority.MEDIUM
    );

    // Update recommendation
    performanceRecommendation.setRecommendationState(RecommendationState.RESOLVED);
    performanceRecommendationService.update(performanceRecommendation);

    // Get updated recommendation
    PerformanceRecommendation recommendation = performanceRecommendationService.get(performanceRecommendation.getId());
    assertEquals(performanceRecommendation.getId(), recommendation.getId());
    assertEquals(RecommendationState.RESOLVED, performanceRecommendation.getRecommendationState());
  }

  private static Stream<Arguments> getPerformanceRecommendationValues() {
    Map<String, Object> recInfo = new HashMap<>();
    recInfo.put("sample-key", "sample-value");

    UUID universeId = UUID.randomUUID();
    String recommendation="recommendation";
    String observation="observation";
    RecommendationType recommendationType=RecommendationType.QUERY_LOAD_SKEW;
    EntityType entityType=EntityType.NODE;
    String entityNames="node-1, node-2";
    Map<String, Object> recommendationInfo=recInfo;
    RecommendationState recommendationState=RecommendationState.OPEN;
    RecommendationPriority recommendationPriority=RecommendationPriority.MEDIUM;
    return Stream.of(
      arguments(
        null,
        recommendation,
        observation,
        recommendationType,
        entityType,
        entityNames,
        recommendationInfo,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        null,
        observation,
        recommendationType,
        entityType,
        entityNames,
        recommendationInfo,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        null,
        recommendationType,
        entityType,
        entityNames,
        recommendationInfo,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        observation,
        null,
        entityType,
        entityNames,
        recommendationInfo,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        observation,
        recommendationType,
        null,
        entityNames,
        recommendationInfo,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        observation,
        recommendationType,
        entityType,
        null,
        recommendationInfo,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        observation,
        recommendationType,
        entityType,
        entityNames,
        null,
        recommendationState,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        observation,
        recommendationType,
        entityType,
        entityNames,
        recommendationInfo,
        null,
        recommendationPriority
      ),
      arguments(
        universeId,
        recommendation,
        observation,
        recommendationType,
        entityType,
        entityNames,
        recommendationInfo,
        recommendationState,
        null
      )
    );
  }

  @ParameterizedTest
  @MethodSource("getPerformanceRecommendationValues")
  public void testCreateInvalidPerformanceRecommendation(UUID universeId, String recommendation, String observation, RecommendationType recommendationType,  EntityType entityType, String entityNames, Map<String, Object> recommendationInfo, RecommendationState recommendationState, RecommendationPriority recommendationPriority) {
    assertThrows(DataIntegrityException.class, () -> performanceRecommendationService.create(
      universeId,
      recommendation,
      observation,
      recommendationType,
      entityType,
      entityNames,
      recommendationInfo,
      recommendationState,
      recommendationPriority
    ));
  }

}
