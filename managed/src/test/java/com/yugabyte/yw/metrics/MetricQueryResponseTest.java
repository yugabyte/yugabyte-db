// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.MetricConfig;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.List;

import static org.hamcrest.CoreMatchers.instanceOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryResponseTest {

  @InjectMocks
  MetricQueryResponse metricQueryResponse;

  @Test
  public void testMetricsWithNodePrefix() {
    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n" +
                                         " {\"node_prefix\": \"1-host\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n" +
                                         " {\"node_prefix\": \"1-host\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    JsonNode data = queryResponse.getGraphData(new MetricConfig.Layout());

    assertThat(data, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertEquals(data.size(), 2);
    for (int i = 0; i< data.size(); i++) {
      assertThat(data.get(i).get("name").asText(), allOf(notNullValue(), equalTo("1-host")));
      assertThat(data.get(i).get("type").asText(), allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).get("x"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).get("y"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }
  }

  @Test
  public void testSingleMetrics() {
    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n" +
                                         " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    JsonNode data = queryResponse.getGraphData(new MetricConfig.Layout());
    assertThat(data, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertEquals(data.size(), 1);
    assertThat(data.get(0).get("name").asText(), allOf(notNullValue(), equalTo("system")));
    assertThat(data.get(0).get("type").asText(), allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).get("x"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).get("y"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
  }

  @Test
  public void testMultipleMetrics() {
    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"memory\":\"buffered\"},\"values\":[[1479281730,\"0\"],\n" +
                                       " [1479281732,\"0\"],[1479281734,\"0\"]]},{\"metric\":{\"memory\":\"cached\"},\"values\":[[1479281730,\"2902821546.6666665\"],[1479281732,\"2901345621.3333335\"],\n" +
                                       " [1479281734,\"2901345621.3333335\"]]},{\"metric\":{\"memory\":\"free\"},\"values\":[[1479281730,\"137700693.33333334\"],[1479281732,\"139952128\"],\n" +
                                       " [1479281734,\"139952128\"]]},{\"metric\":{\"memory\":\"used\"},\"values\":[[1479281730,\"4193042432\"],[1479281732,\"4192290133.3333335\"],[1479281734,\"4192290133.3333335\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    JsonNode data = queryResponse.getGraphData(new MetricConfig.Layout());
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
  }

  @Test
  public void testMetricsWithAlias() {
    JsonNode responseJson = Json.parse("{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[\n" +
      "{\"metric\":{\"type\":\"rpc_RedisServerService_get.num\"},\"values\":[[1480527366,\"0\"],[1480527387,\"0\"]]},\n" +
      "{\"metric\":{\"type\":\"rpc_RedisServerService_set.num\"},\"values\":[[1480527366,\"0\"],[1480527387,\"0\"]]},\n" +
      "{\"metric\":{\"type\":\"rpc_RedisServerService_echo.num\"},\"values\":[[1480527366,\"0\"],[1480527387,\"0\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    MetricConfig.Layout layout = new MetricConfig.Layout();
    layout.title = "Redis Metrics";
    layout.yaxis = new MetricConfig.Layout.Axis();
    layout.yaxis.alias = ImmutableMap.of("rpc_RedisServerService_get.num", "Get",
                                         "rpc_RedisServerService_set.num", "Set",
                                         "rpc_RedisServerService_echo.num", "Echo");

    JsonNode data = queryResponse.getGraphData(layout);
    assertThat(data, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertEquals(data.size(), 3);
    assertThat(data.get(0).get("type").asText(), allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).get("x"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).get("y"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).get("name").asText(), allOf(notNullValue(), equalTo("Get")));
    assertThat(data.get(1).get("name").asText(), allOf(notNullValue(), equalTo("Set")));
    assertThat(data.get(2).get("name").asText(), allOf(notNullValue(), equalTo("Echo")));
  }
}
