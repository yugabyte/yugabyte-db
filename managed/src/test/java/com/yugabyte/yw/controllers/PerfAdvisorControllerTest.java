// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import io.zonky.test.db.postgres.embedded.EmbeddedPostgres;
import java.io.IOException;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.perf_advisor.filters.PerformanceRecommendationFilter;
import org.yb.perf_advisor.filters.StateChangeAuditInfoFilter;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.EntityType;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationPriority;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationState;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationType;
import org.yb.perf_advisor.models.PerformanceRecommendation.SortBy;
import org.yb.perf_advisor.models.StateChangeAuditInfo;
import org.yb.perf_advisor.models.paging.PagedQuery.SortDirection;
import org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedQuery;
import org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedResponse;
import org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedQuery;
import org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedResponse;
import org.yb.perf_advisor.services.db.PerformanceRecommendationService;
import play.Application;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class PerfAdvisorControllerTest extends FakeDBApplication {

  private static EmbeddedPostgres db;

  private Customer customer;

  private Users user;

  private String authToken;

  private Universe universe;

  private PerformanceRecommendationService performanceRecommendationService;

  {
    try {
      db = EmbeddedPostgres.builder().start();
      Thread shutdownHook =
          new Thread("Stop postgres DB") {
            @Override
            public void run() {
              try {
                db.close();
              } catch (IOException e) {
                throw new RuntimeException(e);
              }
            }
          };
      Runtime.getRuntime().addShutdownHook(shutdownHook);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  private static final Map<String, Object> PERF_ADVISOR_DB_PROPERTIES =
      ImmutableMap.<String, Object>builder()
          .put("db.perf_advisor.driver", "org.postgresql.Driver")
          .put("db.perf_advisor.migration.auto", true)
          .build();

  @Override
  protected Application provideApplication() {
    Map<String, Object> additionalConfiguration = new HashMap<>();
    try {
      DataSource postgresDatabase = db.getPostgresDatabase();
      additionalConfiguration.put(
          "db.perf_advisor.url", postgresDatabase.getConnection().getMetaData().getURL());
      additionalConfiguration.put(
          "db.perf_advisor.username", postgresDatabase.getConnection().getMetaData().getUserName());
      additionalConfiguration.putAll(PERF_ADVISOR_DB_PROPERTIES);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return provideApplication(additionalConfiguration);
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    universe = ModelFactory.createUniverse();
    performanceRecommendationService =
        app.injector().instanceOf(PerformanceRecommendationService.class);
  }

  @Test
  public void testPageRecommendations() {
    PerformanceRecommendation recommendation1 = createTestRecommendation();
    PerformanceRecommendation recommendation2 =
        createTestRecommendation().setRecommendationState(RecommendationState.HIDDEN);
    PerformanceRecommendation recommendation3 =
        createTestRecommendation().setRecommendationState(RecommendationState.RESOLVED);
    performanceRecommendationService.save(
        ImmutableList.of(recommendation1, recommendation2, recommendation3));

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
    StateChangeAuditInfo.db("perf_advisor").save(info1);
    StateChangeAuditInfo info2 =
        createAuditInfo(recommendation, TestUtils.replaceFirstChar(baseUuid, 'b'));
    StateChangeAuditInfo.db("perf_advisor").save(info2);
    StateChangeAuditInfo info3 =
        createAuditInfo(recommendation, TestUtils.replaceFirstChar(baseUuid, 'c'));
    StateChangeAuditInfo.db("perf_advisor").save(info3);

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

  private PerformanceRecommendation createTestRecommendation() {
    Map<String, Object> recInfo = new HashMap<>();
    recInfo.put("sample-key", "sample-value");
    return new PerformanceRecommendation()
        .setRecommendationType(RecommendationType.CPU_SKEW)
        .setRecommendation("recommendation")
        .setObservation("observation")
        .setCustomerId(customer.getUuid())
        .setUniverseId(universe.getUniverseUUID())
        .setEntityType(EntityType.NODE)
        .setEntityNames("node-1, node-2")
        .setRecommendationInfo(recInfo)
        .setRecommendationState(RecommendationState.OPEN)
        .setRecommendationPriority(RecommendationPriority.MEDIUM);
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
        .setTimestamp(Instant.now());
  }
}
