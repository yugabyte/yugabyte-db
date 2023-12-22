// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.MetricConfig;
import java.util.HashMap;
import java.util.Map;
import org.hamcrest.core.AllOf;
import org.hamcrest.core.IsEqual;
import org.hamcrest.core.IsInstanceOf;
import org.hamcrest.core.IsNull;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryExecutorTest extends FakeDBApplication {
  @Mock Config mockAppConfig;

  @Mock ApiHelper mockApiHelper;

  @Mock RuntimeConfGetter runtimeConfGetter;

  private MetricUrlProvider metricUrlProvider;
  private MetricConfig validMetric;
  private MetricConfig validRangeMetric;

  @Before
  public void setUp() {
    when(mockAppConfig.getString("yb.metrics.url")).thenReturn("foo://bar/api/v1");
    when(runtimeConfGetter.getStaticConf()).thenReturn(mockAppConfig);
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsLinkUseBrowserFqdn))
        .thenReturn(true);

    JsonNode configJson =
        Json.parse(
            "{\"metric\": \"our_valid_metric\", "
                + "\"function\": \"sum\", \"filters\": {\"filter\": \"awesome\"},"
                + "\"layout\": {\"title\": \"Awesome Metric\", "
                + "\"xaxis\": { \"type\": \"date\" }}}");
    validMetric = MetricConfig.create("valid_metric", configJson);
    validMetric.save();

    JsonNode rangeConfigJson =
        Json.parse(
            "{\"metric\": \"our_valid_range_metric\", "
                + "\"function\": \"avg_over_time|avg\", \"range\": true,"
                + "\"filters\": {\"filter\": \"awesome\"},"
                + "\"layout\": {\"title\": \"Awesome Metric\", "
                + "\"xaxis\": { \"type\": \"date\" }}}");
    validRangeMetric = MetricConfig.create("valid_range_metric", rangeConfigJson);
    validRangeMetric.save();

    metricUrlProvider = new MetricUrlProvider(runtimeConfGetter);
  }

  @Test
  public void testWithValidMetric() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider, mockApiHelper, new HashMap<>(), params, new HashMap<>());

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/api/v1/query"), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));

    JsonNode result = qe.call();
    assertThat(
        result.get("queryKey").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("valid_metric")));

    JsonNode data = result.get("data");
    assertThat(data, AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertEquals(data.size(), 2);
    for (int i = 0; i < data.size(); i++) {
      assertThat(
          data.get(i).get("name").asText(),
          AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("system")));
      assertThat(
          data.get(i).get("type").asText(),
          AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("scatter")));
      assertThat(
          data.get(i).get("x"),
          AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
      assertThat(
          data.get(i).get("y"),
          AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    }

    JsonNode layout = result.get("layout");
    assertThat(layout, AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertThat(
        layout.get("title").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("Awesome Metric")));
    assertThat(
        layout.get("xaxis"),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertThat(
        layout.get("xaxis").get("type").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("date")));
  }

  @Test
  public void testWithValidRangeMetric() {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_range_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider,
            mockApiHelper,
            new HashMap<>(),
            params,
            new HashMap<>(),
            new MetricSettings()
                .setMetric("valid_range_metric")
                .setNodeAggregation(NodeAggregation.MAX)
                .setTimeAggregation(TimeAggregation.MAX),
            false);

    JsonNode result = qe.call();
    ArrayNode directUrls = (ArrayNode) result.get("directURLs");
    assertEquals(directUrls.size(), 1);
    assertEquals(
        directUrls.get(0).asText(),
        "foo://bar/graph?g0.expr=max%28max_over_time%28"
            + "our_valid_range_metric%7Bfilter%3D%22awesome%22%7D%5B0s%5D%29%29&g0.tab=0"
            + "&g0.range_input=3600s&g0.end_input=");
  }

  @Test
  public void testCustomExternalMetricUrl() {

    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsExternalUrl))
        .thenReturn("bar://external");
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_range_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider,
            mockApiHelper,
            new HashMap<>(),
            params,
            new HashMap<>(),
            new MetricSettings()
                .setMetric("valid_range_metric")
                .setNodeAggregation(NodeAggregation.MAX)
                .setTimeAggregation(TimeAggregation.MAX),
            false);

    JsonNode result = qe.call();
    ArrayNode directUrls = (ArrayNode) result.get("directURLs");
    assertEquals(directUrls.size(), 1);
    assertEquals(
        directUrls.get(0).asText(),
        "bar://external/graph?g0.expr=max%28max_over_time%28"
            + "our_valid_range_metric%7Bfilter%3D%22awesome%22%7D%5B0s%5D%29%29&g0.tab=0"
            + "&g0.range_input=3600s&g0.end_input=");
  }

  @Test
  public void testTopNodesQuery() {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("end", "1479381737");
    params.put("step", "60");
    params.put("range", "3600");
    params.put("queryKey", "valid_range_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider,
            mockApiHelper,
            new HashMap<>(),
            params,
            new HashMap<>(),
            new MetricSettings()
                .setMetric("valid_range_metric")
                .setNodeAggregation(NodeAggregation.MAX)
                .setTimeAggregation(TimeAggregation.MAX)
                .setSplitType(SplitType.NODE)
                .setSplitMode(SplitMode.TOP)
                .setSplitCount(2),
            false);

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\",\"exported_instance\":\"instance1\"},"
                + "\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\",\"exported_instance\":\"instance2\"},"
                + "\"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/api/v1/query_range"), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));

    JsonNode result = qe.call();
    ArrayNode directUrls = (ArrayNode) result.get("directURLs");
    assertEquals(directUrls.size(), 1);
    assertEquals(
        "foo://bar/graph?g0.expr=%28max%28max_over_time%28our_valid_range_metric%7Bfilter"
            + "%3D%22awesome%22%7D%5B60s%5D%29%29+by+%28exported_instance%29+and+topk%282%2C+max%28"
            + "max_over_time%28our_valid_range_metric%7Bfilter%3D%22awesome%22%7D%5B3600s%5D%"
            + "401479381737%29%29+by+%28exported_instance%29%29%29+or+max%28max_over_time%28"
            + "our_valid_range_metric%7Bfilter%3D%22awesome%22%7D%5B60s%5D%29%29&g0.tab=0&"
            + "g0.range_input=100000s&g0.end_input=2016-11-17 11:22:17",
        directUrls.get(0).asText());
  }

  @Test
  public void testWithInvalidMetric() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "invalid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider, mockApiHelper, new HashMap<>(), params, new HashMap<>());
    JsonNode result = qe.call();

    assertThat(
        result.get("queryKey").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("invalid_metric")));
    assertThat(
        result.get("error").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("Invalid Query Key")));
  }

  @Test
  public void testQueryWithEndDate() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("end", "1479281937");
    params.put("queryKey", "valid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider, mockApiHelper, new HashMap<>(), params, new HashMap<>());

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));
    qe.call();
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(
        queryUrl.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("foo://bar/api/v1/query_range")));

    assertThat(
        queryParam.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));
    assertThat(
        queryParam.getValue().toString(),
        AllOf.allOf(
            IsNull.notNullValue(),
            IsEqual.equalTo(
                "{start=1479281737, queryKey=valid_metric, "
                    + "end=1479281937, query=sum(our_valid_metric{filter=\"awesome\"})}")));
  }

  @Test
  public void testQueryWithoutEndDate() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider, mockApiHelper, new HashMap<>(), params, new HashMap<>());

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));
    qe.call();
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(
        queryUrl.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("foo://bar/api/v1/query")));

    assertThat(
        queryParam.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));
    assertThat(
        queryParam.getValue().toString(),
        AllOf.allOf(
            IsNull.notNullValue(),
            IsEqual.equalTo(
                "{start=1479281737, queryKey="
                    + "valid_metric, query=sum(our_valid_metric{filter=\"awesome\"})}")));
  }

  @Test
  public void testInvalidQuery() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            metricUrlProvider, mockApiHelper, new HashMap<>(), params, new HashMap<>());

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"error\",\"errorType\":\"bad_data\","
                + "\"error\":\"parse error at char 44: unexpected \\\"{\\\" in aggregation, expected \\\")\\\"\"}");
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));
    JsonNode response = qe.call();
    assertThat(
        response.get("error").asText(),
        AllOf.allOf(
            IsNull.notNullValue(),
            IsEqual.equalTo(
                "parse error at char 44: unexpected " + "\"{\" in aggregation, expected \")\"")));
  }
}
