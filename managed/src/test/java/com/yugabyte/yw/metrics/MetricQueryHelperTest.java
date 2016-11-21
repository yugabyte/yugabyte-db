// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.HashMap;
import java.util.List;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class MetricQueryHelperTest extends FakeDBApplication {

  @InjectMocks
  MetricQueryHelper metricQueryHelper;

  @Mock
  play.Configuration mockAppConfig;

  @Mock
  ApiHelper mockApiHelper;

  @Before
  public void setUp() {
    when(mockAppConfig.getString("yb.metrics.url")).thenReturn("foo://bar");
  }

  @Test
  public void testQueryWithMultipleMetrics() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "cpu_usage_user");
    params.put("start", "1479281737");
    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n" +
                                         " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n" +
                                         " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/query"), anyMap(), anyMap())).thenReturn(Json.toJson(responseJson));

    JsonNode result = metricQueryHelper.query(params);

    assertThat(result.get("metricKey").asText(), allOf(notNullValue(), equalTo("cpu_usage_user")));

    JsonNode data = result.get("data");
    assertThat(data, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertEquals(data.size(), 2);
    for (int i = 0; i< data.size(); i++) {
      assertThat(data.get(i).get("name").asText(), allOf(notNullValue(), equalTo("system")));
      assertThat(data.get(i).get("type").asText(), allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).get("x"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).get("y"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }

    JsonNode layout = result.get("layout");
    assertThat(layout, allOf(notNullValue(), instanceOf(ObjectNode.class)));
    assertThat(layout.get("title").asText(), allOf(notNullValue(), equalTo("CPU Usage (User)")));
    assertThat(layout.get("xaxis"), allOf(notNullValue(), instanceOf(ObjectNode.class)));
    assertThat(layout.get("xaxis").get("type").asText(), allOf(notNullValue(), equalTo("date")));
  }

  @Test
  public void testQueryWithSingleMetric() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "cpu_usage_user");
    params.put("start", "1479281737");
    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n" +
                                         " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/query"), anyMap(), anyMap())).thenReturn(Json.toJson(responseJson));

    JsonNode result = metricQueryHelper.query(params);

    assertThat(result.get("metricKey").asText(), allOf(notNullValue(), equalTo("cpu_usage_user")));

    JsonNode data = result.get("data");
    assertThat(data, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertEquals(data.size(), 1);
    assertThat(data.get(0).get("name").asText(), allOf(notNullValue(), equalTo("system")));
    assertThat(data.get(0).get("type").asText(), allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).get("x"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).get("y"), allOf(notNullValue(), instanceOf(ArrayNode.class)));

    JsonNode layout = result.get("layout");
    assertThat(layout, allOf(notNullValue(), instanceOf(ObjectNode.class)));
    assertThat(layout.get("title").asText(), allOf(notNullValue(), equalTo("CPU Usage (User)")));
    assertThat(layout.get("xaxis"), allOf(notNullValue(), instanceOf(ObjectNode.class)));
    assertThat(layout.get("xaxis").get("type").asText(), allOf(notNullValue(), equalTo("date")));
  }

  @Test
  public void testQueryRangeWithMultipleMetrics() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "memory_usage");
    params.put("start", "1479281730");
    params.put("end", "1479281734");

    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"memory\":\"buffered\"},\"values\":[[1479281730,\"0\"],\n" +
                                       " [1479281732,\"0\"],[1479281734,\"0\"]]},{\"metric\":{\"memory\":\"cached\"},\"values\":[[1479281730,\"2902821546.6666665\"],[1479281732,\"2901345621.3333335\"],\n" +
                                       " [1479281734,\"2901345621.3333335\"]]},{\"metric\":{\"memory\":\"free\"},\"values\":[[1479281730,\"137700693.33333334\"],[1479281732,\"139952128\"],\n" +
                                       " [1479281734,\"139952128\"]]},{\"metric\":{\"memory\":\"used\"},\"values\":[[1479281730,\"4193042432\"],[1479281732,\"4192290133.3333335\"],[1479281734,\"4192290133.3333335\"]]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/query_range"), anyMap(), anyMap())).thenReturn(Json.toJson(responseJson));

    JsonNode result = metricQueryHelper.query(params);

    assertThat(result.get("metricKey").asText(), allOf(notNullValue(), equalTo("memory_usage")));

    JsonNode data = result.get("data");
    assertThat(data, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    List<JsonNode> memoryTags = responseJson.findValues("memory");

    assertEquals(4, data.size());
    for (int i = 0; i< data.size(); i++) {
      assertThat(data.get(i).get("name").asText(), allOf(notNullValue()));
      assertTrue(memoryTags.contains(data.get(i).get("name")));
      assertThat(data.get(i).get("type").asText(), allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).get("x"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).get("y"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }

    JsonNode layout = result.get("layout");
    assertThat(layout, allOf(notNullValue(), instanceOf(ObjectNode.class)));
    assertThat(layout.get("title").asText(), allOf(notNullValue(), equalTo("Memory Usage")));
    assertThat(layout.get("xaxis"), allOf(notNullValue(), instanceOf(ObjectNode.class)));
    assertThat(layout.get("xaxis").get("type").asText(), allOf(notNullValue(), equalTo("date")));
  }

  @Test
  public void testQueryWithFilters() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "cpu_usage_user");
    params.put("start", "1479281730");
    params.put("end", "1479281734");
    ObjectNode filterJson = Json.newObject();
    filterJson.put("foo", "bar");
    params.put("filters", Json.stringify(filterJson));

    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n" +
                                         " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    HashMap<String, String> queryParams = new HashMap<>();
    queryParams.put("query", "avg(collectd_cpu_percent{cpu=\"user\", foo=\"bar\"})");
    queryParams.put("start", params.get("start"));
    queryParams.put("end", params.get("end"));
    queryParams.put("step", "0");
    queryParams.put("filters", "{\"foo\":\"bar\"}");

    when(mockApiHelper.getRequest(eq("foo://bar/query_range"), anyMap(), eq(queryParams))).thenReturn(Json.toJson(responseJson));
    metricQueryHelper.query(params);
  }

  @Test
  public void testQueryForLargerTimeframe() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "cpu_usage_user");
    params.put("start", "1439281730");
    params.put("end", "1479281734");

    HashMap<String, String> queryParams = new HashMap<>();
    queryParams.put("query", "avg(collectd_cpu_percent{cpu=\"user\"})");
    queryParams.put("start", params.get("start"));
    queryParams.put("end", params.get("end"));
    queryParams.put("step", "400000");
    JsonNode responseJson = Json.parse("{\"status\":null,\"data\":{\"resultType\":null,\"result\":[]}," +
                                         "\"errorType\":null,\"error\":null}");
    when(mockApiHelper.getRequest(eq("foo://bar/query_range"), anyMap(), eq(queryParams))).thenReturn(responseJson);
    metricQueryHelper.query(params);
    verify(mockApiHelper, times(1)).getRequest(eq("foo://bar/query_range"), anyMap(), eq(queryParams));
  }

  @Test
  public void testQueryWithInvalidFilterParam() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "cpu_usage_user");
    params.put("start", "1479281730");
    params.put("end", "1479281734");
    params.put("filters", "foo:bar");

    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n" +
                                         " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/query_range"), anyMap(), anyMap())).thenReturn(Json.toJson(responseJson));
    try {
      metricQueryHelper.query(params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), equalTo("Invalid filter params provided, it should be a hash")));
    }
  }

  @Test
  public void testQueryWithInvalidParams() {
    HashMap<String, String> params = new HashMap<>();
    params.put("metricKey", "cpu_usage_user");
    params.put("start", "1479281730");
    params.put("end", "1479281734");

    JsonNode responseJson = Json.parse("{\"status\":\"error\",\"errorType\":\"bad_data\",\"error\":\"cannot parse \\\"\\\" to a valid duration\"}");

    when(mockApiHelper.getRequest(eq("foo://bar/query_range"), anyMap(), anyMap())).thenReturn(Json.toJson(responseJson));
    JsonNode result = metricQueryHelper.query(params);
    assertThat(result.get("error").asText(), allOf(notNullValue(), equalTo("cannot parse \"\" to a valid duration")));
  }
}
