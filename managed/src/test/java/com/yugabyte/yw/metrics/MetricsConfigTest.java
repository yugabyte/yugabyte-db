// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.BeforeClass;
import org.junit.Test;
import play.Application;
import play.libs.Yaml;
import play.test.Helpers;

import java.util.HashMap;
import java.util.Map;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertThat;

public class MetricsConfigTest {

  private static HashMap<String, MetricConfig> metricsConfig = new HashMap<>();

  @BeforeClass
  public static void beforeClass() {
    Application app = Helpers.fakeApplication();
    Helpers.start(app);

    ObjectMapper mapper = new ObjectMapper();
    Map<String, Object> configs = (HashMap<String, Object>) Yaml.load("metrics.yml");
    for(Map.Entry<String, Object> config : configs.entrySet()) {
      metricsConfig.put(config.getKey(), mapper.convertValue(config.getValue(), MetricConfig.class));
    }
  }

  @Test
  public void testMetricsYaml() {
    // Make sure all the configs inside of metrics yaml are valid.
    for(Map.Entry<String, MetricConfig> entry : metricsConfig.entrySet()) {
      assertThat(entry.getValue().getQuery(new HashMap<>()), allOf(notNullValue()));
    }
  }

  @Test
  public void testQueryFailure() {
    MetricConfig metricConfig = new MetricConfig();
    try {
      metricConfig.getQuery(new HashMap<>());
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), equalTo("Invalid MetricConfig: metric attribute is required")));
    }
  }

  @Test
  public void testSimpleQuery() {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.metric = "metric";
    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("metric")));
  }

  @Test
  public void testQueryWithFunctionAndRange() {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.metric = "metric";
    metricConfig.range = "30m";
    metricConfig.function = "rate";

    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric[30m])")));
  }

  @Test
  public void testQueryWithRangeWithoutFunction() {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.metric = "metric";
    metricConfig.range = "30m";

    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("metric")));
  }

  @Test
  public void testQueryWithSingleFilters() {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.metric = "metric";
    metricConfig.range = "30m";
    metricConfig.function = "rate";
    metricConfig.filters.put("memory", "used");

    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{memory=\"used\"}[30m])")));
  }

  @Test
  public void testQueryWithAdditionalFilters() {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.metric = "metric";
    metricConfig.range = "30m";
    metricConfig.function = "rate";
    metricConfig.filters.put("memory", "used");

    HashMap<String, String> extraFilters = new HashMap<>();
    extraFilters.put("extra", "1");
    String query = metricConfig.getQuery(extraFilters);
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{memory=\"used\", extra=\"1\"}[30m])")));
  }

  @Test
  public void testQueryWithComplexFilters() {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.metric = "metric";
    metricConfig.range = "30m";
    metricConfig.function = "rate";
    metricConfig.filters.put("memory", "used|buffered|free");

    String query = metricConfig.getQuery(new HashMap<>());
    assertThat(query, allOf(notNullValue(), equalTo("rate(metric{memory=~\"used|buffered|free\"}[30m])")));
  }
}
