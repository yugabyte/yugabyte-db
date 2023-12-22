// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import static com.yugabyte.yw.metrics.MetricQueryHelper.STEP_SIZE;
import static junit.framework.TestCase.assertTrue;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.AllOf.allOf;
import static org.hamcrest.core.Is.is;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.metrics.data.AlertData;
import com.yugabyte.yw.metrics.data.AlertState;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.MetricConfigDefinition;
import java.io.IOException;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.util.*;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import org.hamcrest.CoreMatchers;
import org.hamcrest.Matchers;
import org.hamcrest.core.IsInstanceOf;
import org.joda.time.DateTime;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryHelperTest extends FakeDBApplication {

  MetricQueryHelper metricQueryHelper;

  @Mock Config mockAppConfig;

  @Mock PlatformExecutorFactory mockPlatformExecutorFactory;

  @Mock RuntimeConfGetter runtimeConfGetter;

  @Mock WSClientRefresher wsClientRefresher;

  MetricConfigDefinition validMetric;

  @Before
  public void setUp() {
    JsonNode configJson = Json.parse("{\"metric\": \"my_valid_metric\", \"function\": \"sum\"}");
    MetricConfig metricConfig = MetricConfig.create("valid_metric", configJson);
    metricConfig.save();
    validMetric = metricConfig.getConfig();
    ThreadPoolExecutor executor = (ThreadPoolExecutor) Executors.newFixedThreadPool(1);
    when(mockAppConfig.getString("yb.metrics.url")).thenReturn("foo://bar/api/v1");
    when(mockAppConfig.getString("yb.metrics.scrape_interval")).thenReturn("1s");
    when(mockPlatformExecutorFactory.createFixedExecutor(any(), anyInt(), any()))
        .thenReturn(executor);
    when(runtimeConfGetter.getStaticConf()).thenReturn(mockAppConfig);
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsAuth)).thenReturn(false);
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsLinkUseBrowserFqdn))
        .thenReturn(true);

    MetricUrlProvider metricUrlProvider = new MetricUrlProvider(runtimeConfGetter);
    metricQueryHelper =
        new MetricQueryHelper(
            mockAppConfig,
            runtimeConfGetter,
            wsClientRefresher,
            metricUrlProvider,
            mockPlatformExecutorFactory) {
          @Override
          protected ApiHelper getApiHelper() {
            return mockApiHelper;
          }
        };
  }

  @Test
  public void testQueryWithInvalidParams() {
    try {
      metricQueryHelper.query(Collections.emptyList(), Collections.emptyMap());
    } catch (PlatformServiceException re) {
      AssertHelper.assertBadRequest(
          re.buildResult(fakeRequest), "Empty metricsWithSettings data provided.");
    }
  }

  @Test
  public void testQueryWithInvalidFilterParams() {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("filters", "my-own-filter");

    try {
      metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      AssertHelper.assertBadRequest(
          re.buildResult(fakeRequest), "Invalid filter params provided, it should be a hash.");
    }
  }

  @Test
  public void testQueryShortDifference() {
    HashMap<String, String> params = new HashMap<>();
    long startTimestamp = 1646925800;
    params.put("start", String.valueOf(startTimestamp));
    params.put("end", String.valueOf(startTimestamp - 1));

    try {
      metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      AssertHelper.assertBadRequest(
          re.buildResult(fakeRequest), "Queried time interval should be positive");
    }
  }

  @Test
  public void testQueryInvalidStep() {
    HashMap<String, String> params = new HashMap<>();
    long startTimestamp = 1646925800;
    params.put("start", String.valueOf(startTimestamp));
    params.put("end", String.valueOf(startTimestamp - STEP_SIZE + 1));
    params.put("step", "qwe");

    try {
      metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      final Result result = re.buildResult(fakeRequest);
      AssertHelper.assertBadRequest(result, "Step should be a valid integer");
    }
  }

  @Test
  public void testQueryLowStep() {
    HashMap<String, String> params = new HashMap<>();
    long startTimestamp = 1646925800;
    params.put("start", String.valueOf(startTimestamp));
    params.put("end", String.valueOf(startTimestamp - STEP_SIZE + 1));
    params.put("step", "0");

    try {
      metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      String expectedErr = "Step should not be less than 1 second";
      AssertHelper.assertBadRequest(re.buildResult(fakeRequest), expectedErr);
    }
  }

  @Test
  public void testQuerySingleMetricWithoutEndTime() {
    DateTime date = DateTime.now().minusSeconds(STEP_SIZE + 100);
    Integer startTimestamp = Math.toIntExact(date.getMillis() / 1000);
    HashMap<String, String> params = new HashMap<>();
    params.put("start", startTimestamp.toString());

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    Map<String, String> graphQueryParam = queryParam.getValue();
    assertThat(
        graphQueryParam.get("query"), allOf(notNullValue(), equalTo("sum(my_valid_metric)")));
    assertThat(
        Integer.parseInt(graphQueryParam.get("time")),
        allOf(notNullValue(), equalTo(startTimestamp)));
    assertThat(Integer.parseInt(graphQueryParam.get("step")), is(notNullValue()));
    assertThat(Integer.parseInt(graphQueryParam.get("_")), is(notNullValue()));
  }

  @Test
  public void testQuerySingleMetricWithEndTime() {
    DateTime date = DateTime.now();
    long startTimestamp = date.minusMinutes(10).getMillis() / 1000;
    long endTimestamp = date.getMillis() / 1000;
    HashMap<String, String> params = new HashMap<>();
    params.put("start", Long.toString(startTimestamp));
    params.put("end", Long.toString(endTimestamp));
    long timeDifference = endTimestamp - startTimestamp;
    int step = Math.round(timeDifference / 100);
    long adjustedStartTimestamp = startTimestamp - startTimestamp % step;
    long adjustedEndTimestamp = endTimestamp - startTimestamp % step;
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query_range")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    Map<String, String> graphQueryParam = queryParam.getValue();
    assertThat(
        graphQueryParam.get("query"), allOf(notNullValue(), equalTo("sum(my_valid_metric)")));
    assertThat(
        Long.parseLong(graphQueryParam.get("start")),
        allOf(notNullValue(), equalTo(adjustedStartTimestamp)));
    assertThat(
        Long.parseLong(graphQueryParam.get("end")),
        allOf(notNullValue(), equalTo(adjustedEndTimestamp)));
    assertThat(Integer.parseInt(graphQueryParam.get("step")), allOf(notNullValue(), equalTo(6)));
  }

  @Test
  public void testQuerySingleMetricWithStep() {
    DateTime date = DateTime.now();
    long startTimestamp = date.minusMinutes(1).getMillis() / 1000;
    long endTimestamp = date.getMillis() / 1000;
    HashMap<String, String> params = new HashMap<>();
    params.put("start", Long.toString(startTimestamp));
    params.put("end", Long.toString(endTimestamp));
    params.put("step", "30");
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    int step = 30;
    long adjustedStartTimestamp = startTimestamp - startTimestamp % step;
    long adjustedEndTimestamp = endTimestamp - startTimestamp % step;

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    metricQueryHelper.query(ImmutableList.of("valid_metric"), params);
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query_range")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    Map<String, String> graphQueryParam = queryParam.getValue();
    assertThat(
        graphQueryParam.get("query"), allOf(notNullValue(), equalTo("sum(my_valid_metric)")));
    assertThat(
        Long.parseLong(graphQueryParam.get("start")),
        allOf(notNullValue(), equalTo(adjustedStartTimestamp)));
    assertThat(
        Long.parseLong(graphQueryParam.get("end")),
        allOf(notNullValue(), equalTo(adjustedEndTimestamp)));
    assertThat(Integer.parseInt(graphQueryParam.get("step")), allOf(notNullValue(), equalTo(step)));
  }

  @Test
  public void testDirectQuerySingleValue() {

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"__name__\":\"foobar\", \"node_prefix\":\"yb-test-1\"},\"value\":"
                + "[1479278137,\"0.027751899056199826\"]}]}}");

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);

    ArrayList<MetricQueryResponse.Entry> results = metricQueryHelper.queryDirect("foobar");
    assertEquals(results.size(), 1);
    assertEquals(results.get(0).labels.size(), 2);
    assertEquals(results.get(0).labels.get("node_prefix"), "yb-test-1");
    assertEquals(results.get(0).values.size(), 1);
    assertEquals(results.get(0).values.get(0).getLeft().intValue(), 1479278137);
    assertEquals(results.get(0).values.get(0).getRight().doubleValue(), 0.028, 0.005);
  }

  @Test
  public void testDirectQueryMultipleValues() {

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"__name__\":\"foobar\", \"node_prefix\":\"yb-test-1\"},\"values\":"
                + "[[1479278132,\"0.037751899056199826\"], [1479278137,\"0.027751899056199826\"]"
                + "]}]}}");

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);

    ArrayList<MetricQueryResponse.Entry> results = metricQueryHelper.queryDirect("foobar");
    assertEquals(results.size(), 1);
    assertEquals(results.get(0).labels.size(), 2);
    assertEquals(results.get(0).labels.get("node_prefix"), "yb-test-1");
    assertEquals(results.get(0).values.size(), 2);
    assertEquals(results.get(0).values.get(0).getLeft().intValue(), 1479278132);
    assertEquals(results.get(0).values.get(1).getLeft().intValue(), 1479278137);
    assertEquals(results.get(0).values.get(0).getRight().doubleValue(), 0.038, 0.005);
    assertEquals(results.get(0).values.get(1).getRight().doubleValue(), 0.028, 0.005);
  }

  @Test
  public void testQueryMultipleMetrics() {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1481147528");
    params.put("end", "1481147648");

    JsonNode configJson = Json.parse("{\"metric\": \"my_valid_metric2\", \"function\": \"avg\"}");
    MetricConfig metricConfig = MetricConfig.create("valid_metric2", configJson);
    metricConfig.save();
    MetricConfigDefinition validMetric2 = metricConfig.getConfig();

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);
    List<String> metricKeys = ImmutableList.of("valid_metric2", "valid_metric");

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    JsonNode result = metricQueryHelper.query(metricKeys, params);
    verify(mockApiHelper, times(2))
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());
    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query_range")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    List<String> expectedQueryStrings = new ArrayList<>();
    expectedQueryStrings.add(validMetric.getQuery(new HashMap<>(), 60 /* queryRangeSecs */));
    expectedQueryStrings.add(validMetric2.getQuery(new HashMap<>(), 60 /* queryRangeSecs */));

    for (Map<String, String> capturedQueryParam : queryParam.getAllValues()) {
      assertTrue(expectedQueryStrings.contains(capturedQueryParam.get("query")));
      assertTrue(metricKeys.contains(capturedQueryParam.get("queryKey")));
      assertThat(
          Integer.parseInt(capturedQueryParam.get("start").toString()),
          allOf(notNullValue(), equalTo(1481147526)));
      assertThat(
          Integer.parseInt(capturedQueryParam.get("step").toString()),
          allOf(notNullValue(), equalTo(3)));
      assertThat(
          Integer.parseInt(capturedQueryParam.get("end").toString()),
          allOf(notNullValue(), equalTo(1481147646)));
    }
  }

  @Test
  public void testQueryAlerts() throws IOException {
    JsonNode responseJson = Json.parse(TestUtils.readResource("alert/alerts_query.json"));

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);

    when(mockApiHelper.getRequest(anyString(), any())).thenReturn(responseJson);
    List<AlertData> alerts = metricQueryHelper.queryAlerts();
    verify(mockApiHelper).getRequest(queryUrl.capture(), any());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/alerts")));

    AlertData alertData =
        AlertData.builder()
            .activeAt(
                ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00")
                    .withZoneSameInstant(ZoneOffset.UTC))
            .annotations(ImmutableMap.of("summary", "Clock Skew Alert for universe Test is firing"))
            .labels(
                ImmutableMap.of(
                    "customer_uuid", "199bccbc-6295-4676-950e-c0049b8adfa9",
                    "definition_uuid", "199bccbc-6295-4676-950e-c0049b8adfa8",
                    "definition_name", "Clock Skew Alert"))
            .state(AlertState.firing)
            .value(1)
            .build();
    assertThat(alerts, Matchers.contains(alertData));
  }

  @Test
  public void testQueryAlertsError() throws IOException {
    JsonNode responseJson = Json.parse(TestUtils.readResource("alert/alerts_query_error.json"));

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);

    when(mockApiHelper.getRequest(anyString(), any())).thenReturn(responseJson);
    try {
      metricQueryHelper.queryAlerts();
    } catch (Exception e) {
      assertThat(e, CoreMatchers.instanceOf(RuntimeException.class));
    }
    verify(mockApiHelper).getRequest(queryUrl.capture(), any());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/alerts")));
  }
}
