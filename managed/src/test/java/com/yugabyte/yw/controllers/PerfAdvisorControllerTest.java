// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakePerfAdvisorDBTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.PerfAdvisorSettingsFormData;
import com.yugabyte.yw.forms.PerfAdvisorSettingsWithDefaults;
import com.yugabyte.yw.models.UniversePerfAdvisorRun;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.perf_advisor.filters.PerformanceRecommendationFilter;
import org.yb.perf_advisor.filters.StateChangeAuditInfoFilter;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationState;
import org.yb.perf_advisor.models.PerformanceRecommendation.SortBy;
import org.yb.perf_advisor.models.StateChangeAuditInfo;
import org.yb.perf_advisor.models.paging.PagedQuery.SortDirection;
import org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedQuery;
import org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedResponse;
import org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedQuery;
import org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedResponse;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class PerfAdvisorControllerTest extends FakePerfAdvisorDBTest {

  private Users user;

  private String authToken;

  @Before
  public void setUp() {
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
  }

  @Test
  public void testPageRecommendations() {
    PerformanceRecommendation recommendation1 = createTestRecommendation();
    PerformanceRecommendation recommendation2 =
        createTestRecommendation().setRecommendationState(RecommendationState.HIDDEN);
    PerformanceRecommendation recommendation3 =
        createTestRecommendation().setRecommendationState(RecommendationState.RESOLVED);
    List<PerformanceRecommendation> recommendations =
        ImmutableList.of(recommendation1, recommendation2, recommendation3);
    performanceRecommendationService.save(recommendations);

    PerformanceRecommendationPagedQuery query = new PerformanceRecommendationPagedQuery();
    query.setSortBy(SortBy.recommendationState);
    query.setDirection(SortDirection.DESC);
    query.setFilter(PerformanceRecommendationFilter.builder().build());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/performance_recommendations/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode json = Json.parse(contentAsString(result));
    PerformanceRecommendationPagedResponse response =
        Json.fromJson(json, PerformanceRecommendationPagedResponse.class);

    assertThat(response.isHasNext(), is(false));
    assertThat(response.isHasPrev(), is(true));
    assertThat(response.getTotalCount(), equalTo(3));
    assertThat(response.getEntities(), hasSize(2));
    assertThat(response.getEntities(), contains(recommendation2, recommendation1));
  }

  @Test
  public void testGetRecommendation() {
    PerformanceRecommendation recommendation = createTestRecommendation();
    recommendation = performanceRecommendationService.save(recommendation);

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/performance_recommendations/"
                + recommendation.getId(),
            authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode json = Json.parse(contentAsString(result));
    PerformanceRecommendation response = Json.fromJson(json, PerformanceRecommendation.class);

    assertThat(response, equalTo(recommendation));
  }

  @Test
  public void testHideRecommendation() {
    PerformanceRecommendation recommendation = createTestRecommendation();
    recommendation = performanceRecommendationService.save(recommendation);

    PerformanceRecommendationFilter filter =
        PerformanceRecommendationFilter.builder().id(recommendation.getId()).build();
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/performance_recommendations/hide",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));

    PerformanceRecommendation updated =
        performanceRecommendationService.get(recommendation.getId());

    assertThat(updated.getRecommendationState(), equalTo(RecommendationState.HIDDEN));
  }

  @Test
  public void testResolveRecommendation() {
    PerformanceRecommendation recommendation = createTestRecommendation();
    recommendation = performanceRecommendationService.save(recommendation);

    PerformanceRecommendationFilter filter =
        PerformanceRecommendationFilter.builder().id(recommendation.getId()).build();
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/performance_recommendations/resolve",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));

    PerformanceRecommendation updated =
        performanceRecommendationService.get(recommendation.getId());

    assertThat(updated.getRecommendationState(), equalTo(RecommendationState.RESOLVED));
  }

  @Test
  public void testDeleteRecommendation() {
    PerformanceRecommendation recommendation = createTestRecommendation();
    recommendation = performanceRecommendationService.save(recommendation);

    PerformanceRecommendationFilter filter =
        PerformanceRecommendationFilter.builder().id(recommendation.getId()).build();
    Result result =
        doRequestWithAuthTokenAndBody(
            "DELETE",
            "/api/customers/" + customer.getUuid() + "/performance_recommendations",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));

    PerformanceRecommendation deleted =
        performanceRecommendationService.get(recommendation.getId());

    assertThat(deleted, nullValue());
  }

  @Test
  public void testPageAuditInfo() {
    PerformanceRecommendation recommendation = createTestRecommendation();
    recommendation = performanceRecommendationService.save(recommendation);

    UUID baseUuid = UUID.randomUUID();
    StateChangeAuditInfo info1 =
        createAuditInfo(recommendation, TestUtils.replaceFirstChar(baseUuid, 'a'));
    DB.byName("perf_advisor").save(info1);
    StateChangeAuditInfo info2 =
        createAuditInfo(recommendation, TestUtils.replaceFirstChar(baseUuid, 'b'));
    DB.byName("perf_advisor").save(info2);
    StateChangeAuditInfo info3 =
        createAuditInfo(recommendation, TestUtils.replaceFirstChar(baseUuid, 'c'));
    DB.byName("perf_advisor").save(info3);

    StateChangeAuditInfoPagedQuery query = new StateChangeAuditInfoPagedQuery();
    query.setSortBy(StateChangeAuditInfo.SortBy.id);
    query.setDirection(SortDirection.ASC);
    query.setFilter(StateChangeAuditInfoFilter.builder().build());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/performance_recommendation_state_change/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode json = Json.parse(contentAsString(result));
    StateChangeAuditInfoPagedResponse response =
        Json.fromJson(json, StateChangeAuditInfoPagedResponse.class);

    assertThat(response.isHasNext(), is(false));
    assertThat(response.isHasPrev(), is(true));
    assertThat(response.getTotalCount(), equalTo(3));
    assertThat(response.getEntities(), hasSize(2));
    assertThat(response.getEntities(), contains(info2, info3));
  }

  @Test
  public void testUpdateAndGetSettingsRecommendation() {
    PerfAdvisorSettingsFormData settings =
        new PerfAdvisorSettingsFormData()
            .setEnabled(true)
            .setUniverseFrequencyMins(10)
            .setConnectionSkewThresholdPct(60.0)
            .setConnectionSkewMinConnections(100)
            .setConnectionSkewIntervalMins(5)
            .setCpuSkewThresholdPct(65.0)
            .setCpuSkewMinUsagePct(55.0)
            .setCpuSkewIntervalMins(6)
            .setCpuUsageThreshold(55.0)
            .setCpuUsageIntervalMins(7)
            .setQuerySkewThresholdPct(70.0)
            .setQuerySkewMinQueries(200)
            .setQuerySkewIntervalMins(8)
            .setRejectedConnThreshold(9)
            .setRejectedConnIntervalMins(15)
            .setHotShardWriteSkewThresholdPct(800.0)
            .setHotShardReadSkewThresholdPct(800.0)
            .setHotShardIntervalMins(10)
            .setHotShardMinNodeWrites(600)
            .setHotShardMinNodeReads(600);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/universes/"
                + universe.getUniverseUUID()
                + "/perf_advisor_settings",
            authToken,
            Json.toJson(settings));
    assertThat(result.status(), equalTo(OK));

    result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/universes/"
                + universe.getUniverseUUID()
                + "/perf_advisor_settings",
            authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode json = Json.parse(contentAsString(result));
    PerfAdvisorSettingsWithDefaults updated =
        Json.fromJson(json, PerfAdvisorSettingsWithDefaults.class);

    assertThat(updated.getUniverseSettings(), equalTo(settings));
    assertThat(updated.getDefaultSettings(), notNullValue());
  }

  @Test
  public void testGetLatestRun() {
    UniversePerfAdvisorRun lastRun =
        UniversePerfAdvisorRun.create(customer.getUuid(), universe.getUniverseUUID(), true);
    lastRun.setScheduleTime(nowWithoutMillis());
    lastRun.save();
    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/universes/"
                + universe.getUniverseUUID()
                + "/last_run",
            authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode json = Json.parse(contentAsString(result));
    UniversePerfAdvisorRun queried = Json.fromJson(json, UniversePerfAdvisorRun.class);

    assertThat(lastRun, equalTo(queried));
  }

  private StateChangeAuditInfo createAuditInfo(
      PerformanceRecommendation recommendation, UUID uuid) {
    return new StateChangeAuditInfo()
        .setId(uuid)
        .setFieldName("recommendationState")
        .setPreviousValue(RecommendationState.OPEN.name())
        .setUpdatedValue(RecommendationState.HIDDEN.name())
        .setPerformanceRecommendation(recommendation)
        .setCustomerId(customer.getUuid())
        .setUserId(UUID.randomUUID())
        .setTimestamp(Instant.now().truncatedTo(ChronoUnit.MICROS));
  }
}
