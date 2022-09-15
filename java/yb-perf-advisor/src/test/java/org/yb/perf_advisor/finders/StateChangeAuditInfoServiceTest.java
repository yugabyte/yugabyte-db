package org.yb.perf_advisor.finders;

import io.ebean.DataIntegrityException;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.yb.perf_advisor.common.MockServer;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.EntityType;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationPriority;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationState;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationType;
import org.yb.perf_advisor.models.StateChangeAuditInfo;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.params.provider.Arguments.arguments;

public class StateChangeAuditInfoServiceTest extends MockServer {

  private PerformanceRecommendationService performanceRecommendationService;
  private StateChangeAuditInfoService stateChangeAuditInfoService;
  PerformanceRecommendation performanceRecommendation;

  @BeforeEach
  public void setUp() {
    performanceRecommendationService = new PerformanceRecommendationService(ebeanServer);
    stateChangeAuditInfoService = new StateChangeAuditInfoService(ebeanServer);

    Map<String, Object> recInfo = new HashMap<>();
    recInfo.put("sample-key", "sample-value");

    performanceRecommendation = performanceRecommendationService.create(UUID.randomUUID(),
      "recommendation",
      "observation",
      RecommendationType.QUERY_LOAD_SKEW,
      EntityType.NODE,
      "node-1, node-2",
      recInfo,
      RecommendationState.OPEN,
      RecommendationPriority.MEDIUM
    );
  }

  @Test
  public void testCreateAndGetStateChangeAuditInfo() {
    StateChangeAuditInfo stateChangeAuditInfo1 = stateChangeAuditInfoService.create(
      performanceRecommendation,
      "fieldName",
      "value1",
      "value2",
      UUID.randomUUID()
    );

    StateChangeAuditInfo stateChangeAuditInfo2 = stateChangeAuditInfoService.create(
      performanceRecommendation,
      "fieldName",
      "value2",
      "value3",
      UUID.randomUUID()
    );


    // Validate first state change DB entry
    StateChangeAuditInfo stateChangeAuditInfo = stateChangeAuditInfoService.get(stateChangeAuditInfo1.getId());
    assertEquals(stateChangeAuditInfo1.getId(), stateChangeAuditInfo.getId());
    assertEquals(stateChangeAuditInfo1.getFieldName(), stateChangeAuditInfo.getFieldName());
    assertEquals(stateChangeAuditInfo1.getPreviousValue(), stateChangeAuditInfo.getPreviousValue());
    assertEquals(stateChangeAuditInfo1.getUpdatedValue(), stateChangeAuditInfo.getUpdatedValue());
    assertEquals(stateChangeAuditInfo1.getUserId(), stateChangeAuditInfo.getUserId());
    assertEquals(stateChangeAuditInfo1.getTimestamp(), stateChangeAuditInfo.getTimestamp());
    assertEquals(stateChangeAuditInfo1.getPerformanceRecommendation(), stateChangeAuditInfo.getPerformanceRecommendation());

    // Validate second state change DB entry
    stateChangeAuditInfo = stateChangeAuditInfoService.get(stateChangeAuditInfo2.getId());
    assertEquals(stateChangeAuditInfo2.getId(), stateChangeAuditInfo.getId());
    assertEquals(stateChangeAuditInfo2.getFieldName(), stateChangeAuditInfo.getFieldName());
    assertEquals(stateChangeAuditInfo2.getPreviousValue(), stateChangeAuditInfo.getPreviousValue());
    assertEquals(stateChangeAuditInfo2.getUpdatedValue(), stateChangeAuditInfo.getUpdatedValue());
    assertEquals(stateChangeAuditInfo2.getUserId(), stateChangeAuditInfo.getUserId());
    assertEquals(stateChangeAuditInfo2.getTimestamp(), stateChangeAuditInfo.getTimestamp());
    assertEquals(stateChangeAuditInfo2.getPerformanceRecommendation(), stateChangeAuditInfo.getPerformanceRecommendation());

    // Validate state change info list from performance recommendation
    performanceRecommendation = performanceRecommendationService.get(performanceRecommendation.getId());
    assertEquals(2, performanceRecommendation.getStateChangeAuditInfoList().size());
  }

  private static Stream<Arguments> getStateChangeAuditInfoValues() {
    String fieldName="fieldName";
    String previousValue="previousValue";
    String updatedValue="updatedValue";
    UUID userId=UUID.randomUUID();
    return Stream.of(
      arguments(
        true,
        fieldName,
        previousValue,
        updatedValue,
        userId
      ),
      arguments(
        false,
        null,
        previousValue,
        updatedValue,
        userId
      ),
      arguments(
        false,
        fieldName,
        null,
        updatedValue,
        userId
      ),
      arguments(
        false,
        fieldName,
        previousValue,
        null,
        userId
      ),
      arguments(
        false,
        fieldName,
        previousValue,
        updatedValue,
        null
      )
    );
  }

  @ParameterizedTest
  @MethodSource("getStateChangeAuditInfoValues")
  public void testCreateInvalidStateChangeAuditInfo(boolean isPerformanceRecommendationNull, String fieldName, String previousValue, String updatedValue, UUID userId) {
    PerformanceRecommendation recommendation = null;
    if(!isPerformanceRecommendationNull){
      recommendation = performanceRecommendation;
    }
    PerformanceRecommendation finalRecommendation = recommendation;
    assertThrows(DataIntegrityException.class, () -> stateChangeAuditInfoService.create(
      finalRecommendation,
      fieldName,
      previousValue,
      updatedValue,
      userId
    ));
  }
}
