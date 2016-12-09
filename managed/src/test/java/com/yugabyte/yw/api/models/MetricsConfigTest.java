// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.MetricConfig;
import org.junit.Test;
import play.libs.Json;
import play.libs.Yaml;

import java.util.HashMap;
import java.util.Map;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertThat;

public class MetricsConfigTest  extends FakeDBApplication {

  @Test
  public void testMetricsYaml() {
    // Make sure all the configs inside of metrics yaml are valid.
    Map<String, Object> configs = (HashMap<String, Object>) Yaml.load("metrics.yml");
    MetricConfig.loadConfig(configs);
    for(MetricConfig config : MetricConfig.find.all()) {
      assertThat(config.getLayout(), allOf(notNullValue(), instanceOf(MetricConfig.Layout.class)));
      assertThat(config.getQuery(new HashMap<>()), allOf(notNullValue()));
    }
  }

  @Test
  public void testQueryFailure() {
    MetricConfig metricConfig = MetricConfig.create("metric", Json.newObject());
    metricConfig.save();
    try {
      metricConfig.getQuery(new HashMap<>());
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), equalTo("Invalid MetricConfig: metric attribute is required")));
    }
  }

  @Test
  public void testSimpleQuery() {
    MetricConfig metricConfig = MetricConfig.create("metric", Json.parse("{\"metric\": \"metric\"}"));
    metricConfig.save();
    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("metric")));
  }

  @Test
  public void testQueryWithFunctionAndRange() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\", " +
                                       "\"function\": \"rate\"}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();
    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric[30m])")));
  }

  @Test
  public void testQueryWithRangeWithoutFunction() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\"}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();
    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("metric")));
  }

  @Test
  public void testQueryWithSingleFilters() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\"," +
                                       "\"function\": \"rate\", \"filters\": {\"memory\": \"used\"}}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();

    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{memory=\"used\"}[30m])")));
  }

  @Test
  public void testQueryWithAdditionalFiltersWithoutOriginalFilters() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\", \"function\": \"rate\"}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();

    String query = metricConfig.getQuery(ImmutableMap.of("extra", "1"));
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{extra=\"1\"}[30m])")));

  }

  @Test
  public void testQueryWithAdditionalFilters() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\"," +
                                       "\"function\": \"rate\", \"filters\": {\"memory\": \"used\"}}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();

    String query = metricConfig.getQuery(ImmutableMap.of("extra", "1"));
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{memory=\"used\", extra=\"1\"}[30m])")));
  }

  @Test
  public void testQueryWithComplexFilters() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\"," +
                                       "\"function\": \"rate\", \"filters\": {\"memory\": \"used|buffered|free\"}}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();
    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{memory=~\"used|buffered|free\"}[30m])")));
  }

  @Test
  public void testQueryWithMultipleFunctions() {
    JsonNode configJson = Json.parse("{\"metric\": \"metric\", \"range\": \"30m\"," +
                                       "\"function\": \"rate|avg\", \"filters\": {\"memory\": \"used\"}}");
    MetricConfig metricConfig = MetricConfig.create("metric", configJson);
    metricConfig.save();

    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("avg(rate(metric{memory=\"used\"}[30m]))")));
  }
}
