// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.MetricConfigDefinition;
import com.yugabyte.yw.models.MetricConfigDefinition.Layout;
import com.yugabyte.yw.models.MetricConfigDefinition.Layout.Axis;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryResponseTest {

  private static final String METRIC_NAME = "metric_name";
  private static final MetricSettings METRIC_SETTINGS = MetricSettings.defaultSettings(METRIC_NAME);

  @InjectMocks MetricQueryResponse metricQueryResponse;

  @Test
  public void testMetricsWithNodePrefix() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"node_prefix\":"
                + " \"1-host\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"node_prefix\": \"1-host\"},"
                + " \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    List<MetricGraphData> data =
        queryResponse.getGraphData(METRIC_NAME, new MetricConfigDefinition(), METRIC_SETTINGS);

    assertEquals(data.size(), 2);
    for (int i = 0; i < data.size(); i++) {
      assertThat(data.get(i).name, allOf(notNullValue(), equalTo("1-host")));
      assertThat(data.get(i).type, allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }
  }

  @Test
  public void testSingleMetrics() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    List<MetricGraphData> data =
        queryResponse.getGraphData(METRIC_NAME, new MetricConfigDefinition(), METRIC_SETTINGS);
    assertEquals(data.size(), 1);
    assertThat(data.get(0).name, allOf(notNullValue(), equalTo("system")));
    assertThat(data.get(0).type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
  }

  @Test
  public void testMultipleMetrics() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"memory\":\"buffered\"},\"values\":[[1479281730,\"0\"],\n"
                + " [1479281732,\"0\"],[1479281734,\"0\"]]},{\"metric\":{\"memory\":\"cached\"},\"values\":[[1479281730,\"2902821546.6666665\"],[1479281732,\"2901345621.3333335\"],\n"
                + " [1479281734,\"2901345621.3333335\"]]},{\"metric\":{\"memory\":\"free\"},\"values\":[[1479281730,\"137700693.33333334\"],[1479281732,\"139952128\"],\n"
                + " [1479281734,\"139952128\"]]},{\"metric\":{\"memory\":\"used\"},\"values\":[[1479281730,\"4193042432\"],[1479281732,\"4192290133.3333335\"],[1479281734,\"4192290133.3333335\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    List<MetricGraphData> data =
        queryResponse.getGraphData(METRIC_NAME, new MetricConfigDefinition(), METRIC_SETTINGS);
    List<JsonNode> memoryTags = responseJson.findValues("memory");

    assertEquals(4, data.size());
    assertEquals(4, memoryTags.size());
    for (int i = 0; i < data.size(); i++) {
      assertThat(data.get(i).name, allOf(notNullValue()));
      assertThat(memoryTags.get(i).asText(), equalTo(data.get(i).name));
      assertThat(data.get(i).type, allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }
  }

  @Test
  public void testMetricsWithAlias() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"service_method\":\"ExecuteRequest\"},\"values\":[[1507603405,\"4116.2\"]]},{\"metric\":{\"service_method\":\"ParseRequest\"},\"values\":[[1507603405,\"2.8\"]]},{\"metric\":{\"service_method\":\"ProcessRequest\"},\"values\":[[1507603405,\"4138.099999999999\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    Layout layout =
        new Layout()
            .setTitle("CQL Metrics")
            .setYaxis(
                new Axis()
                    .setAlias(
                        ImmutableMap.of(
                            "ExecuteRequest", "Execute",
                            "ParseRequest", "Parse",
                            "ProcessRequest", "Process")));

    MetricConfigDefinition config = new MetricConfigDefinition().setLayout(layout);
    List<MetricGraphData> data = queryResponse.getGraphData(METRIC_NAME, config, METRIC_SETTINGS);
    assertEquals(data.size(), 3);
    assertThat(data.get(0).type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).name, allOf(notNullValue(), equalTo("Execute")));
    assertThat(data.get(1).name, allOf(notNullValue(), equalTo("Parse")));
    assertThat(data.get(2).name, allOf(notNullValue(), equalTo("Process")));
  }

  @Test
  public void testMetricsWithMultiVariableAlias() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"service_method\":\"local\",\"service_type\":\"read\"},\"values\":[[1507603405,\"0.0\"]]},{\"metric\":{\"service_method\":\"local\",\"service_type\":\"write\"},\"values\":[[1507603405,\"0.0\"]]},{\"metric\":{\"service_method\":\"remote\",\"service_type\":\"read\"},\"values\":[[1507603405,\"0.0\"]]},{\"metric\":{\"service_method\":\"remote\",\"service_type\":\"write\"},\"values\":[[1507603405,\"0.0\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    Layout layout =
        new Layout()
            .setTitle("CQL Metrics")
            .setYaxis(
                new Axis()
                    .setAlias(
                        ImmutableMap.of(
                            "remote,read", "Remote Read",
                            "remote,write", "Remote Write",
                            "local,read", "Local Read",
                            "local,write", "Local Write")));

    MetricConfigDefinition config = new MetricConfigDefinition().setLayout(layout);
    List<MetricGraphData> data = queryResponse.getGraphData(METRIC_NAME, config, METRIC_SETTINGS);
    assertEquals(data.size(), 4);
    assertThat(data.get(0).type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    for (int i = 0; i < data.size(); ++i) {
      assertTrue(layout.getYaxis().getAlias().values().contains(data.get(i).name));
    }
  }
}
