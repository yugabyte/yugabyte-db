// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.YWServiceException;
import org.apache.commons.lang.StringUtils;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;

import static play.mvc.Http.Status.BAD_REQUEST;

@Singleton
public class MetricQueryHelper {

  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryHelper.class);
  public static final Integer STEP_SIZE = 100;
  public static final Integer QUERY_EXECUTOR_THREAD_POOL = 5;

  public static final String MANAGEMENT_COMMAND_RELOAD = "reload";
  private static final String PROMETHEUS_METRICS_URL_PATH = "yb.metrics.url";
  private static final String PROMETHEUS_MANAGEMENT_URL_PATH = "yb.metrics.management.url";

  @Inject play.Configuration appConfig;

  @Inject ApiHelper apiHelper;

  @Inject YBMetricQueryComponent ybMetricQueryComponent;

  /**
   * Query prometheus for a given metricType and query params
   *
   * @param params, Query params like start, end timestamps, even filters Ex: {"metricKey":
   *     "cpu_usage_user", "start": <start timestamp>, "end": <end timestamp>}
   * @return MetricQueryResponse Object
   */
  public JsonNode query(List<String> metricKeys, Map<String, String> params) {
    HashMap<String, Map<String, String>> filterOverrides = new HashMap<>();
    return query(metricKeys, params, filterOverrides);
  }

  /**
   * Query prometheus for a given metricType and query params
   *
   * @param params, Query params like start, end timestamps, even filters Ex: {"metricKey":
   *     "cpu_usage_user", "start": <start timestamp>, "end": <end timestamp>}
   * @return MetricQueryResponse Object
   */
  public JsonNode query(
      List<String> metricKeys,
      Map<String, String> params,
      Map<String, Map<String, String>> filterOverrides) {
    if (metricKeys.isEmpty()) {
      throw new YWServiceException(BAD_REQUEST, "Empty metricKeys data provided.");
    }

    long timeDifference;
    if (params.get("end") != null) {
      timeDifference = Long.parseLong(params.get("end")) - Long.parseLong(params.get("start"));
    } else {
      String startTime = params.remove("start");
      int endTime = Math.round(DateTime.now().getMillis() / 1000);
      params.put("time", startTime);
      params.put("_", Integer.toString(endTime));
      timeDifference = endTime - Long.parseLong(startTime);
    }

    if (params.get("step") == null) {
      int resolution = Math.round(timeDifference / STEP_SIZE);
      params.put("step", String.valueOf(resolution));
    }

    HashMap<String, String> additionalFilters = new HashMap<>();
    if (params.containsKey("filters")) {
      try {
        additionalFilters = new ObjectMapper().readValue(params.get("filters"), HashMap.class);
      } catch (IOException e) {
        throw new YWServiceException(
            BAD_REQUEST, "Invalid filter params provided, it should be a hash.");
      }
    }

    String metricsUrl = appConfig.getString(PROMETHEUS_METRICS_URL_PATH);
    boolean useNativeMetrics = appConfig.getBoolean("yb.metrics.useNative", false);
    if ((null == metricsUrl || metricsUrl.isEmpty()) && !useNativeMetrics) {
      LOG.error("Error fetching metrics data: no prometheus metrics URL configured");
      return Json.newObject();
    }

    ExecutorService threadPool = Executors.newFixedThreadPool(QUERY_EXECUTOR_THREAD_POOL);
    Set<Future<JsonNode>> futures = new HashSet<Future<JsonNode>>();
    for (String metricKey : metricKeys) {
      Map<String, String> queryParams = params;
      queryParams.put("queryKey", metricKey);

      Map<String, String> specificFilters = filterOverrides.getOrDefault(metricKey, null);
      if (specificFilters != null) {
        additionalFilters.putAll(specificFilters);
      }

      Callable<JsonNode> callable =
          new MetricQueryExecutor(
              appConfig, apiHelper, queryParams, additionalFilters, ybMetricQueryComponent);
      Future<JsonNode> future = threadPool.submit(callable);
      futures.add(future);
    }

    ObjectNode responseJson = Json.newObject();
    for (Future<JsonNode> future : futures) {
      JsonNode response = Json.newObject();
      try {
        response = future.get();
      } catch (InterruptedException | ExecutionException e) {
        LOG.error("Error fetching metrics data", e);
      }

      responseJson.set(response.get("queryKey").asText(), response);
    }
    threadPool.shutdown();
    return responseJson;
  }

  /**
   * Query Prometheus via HTTP for metric values
   *
   * <p>The main difference between this and regular MetricQueryHelper::query is that it does not
   * depend on the metric config being present in metrics.yml
   *
   * <p>promQueryExpression is a standard prom query expression of the form
   *
   * <p>metric_name{filter_name_optional="filter_value"}[time_expr_optional]
   *
   * <p>for ex: 'up', or 'up{node_prefix="yb-test"}[10m] Without a time expression, only the most
   * recent value is returned.
   *
   * <p>The return type is a set of labels for each metric and an array of time-stamped values
   */
  public ArrayList<MetricQueryResponse.Entry> queryDirect(String promQueryExpression) {
    final String metricsUrl = appConfig.getString(PROMETHEUS_METRICS_URL_PATH);
    if (StringUtils.isEmpty(metricsUrl)) {
      throw new RuntimeException(PROMETHEUS_METRICS_URL_PATH + " not set");
    }
    final String queryUrl = metricsUrl + "/query";

    HashMap<String, String> getParams = new HashMap<>();
    getParams.put("query", promQueryExpression);
    final JsonNode responseJson =
        apiHelper.getRequest(queryUrl, new HashMap<>(), /*headers*/ getParams);
    final MetricQueryResponse metricResponse =
        Json.fromJson(responseJson, MetricQueryResponse.class);
    if (metricResponse.error != null || metricResponse.data == null) {
      throw new RuntimeException("Error querying prometheus metrics: " + responseJson.toString());
    }

    return metricResponse.getValues();
  }

  public void postManagementCommand(String command) {
    final String prometheusManagementUrl = appConfig.getString(PROMETHEUS_MANAGEMENT_URL_PATH);
    if (StringUtils.isEmpty(prometheusManagementUrl)) {
      throw new RuntimeException(PROMETHEUS_MANAGEMENT_URL_PATH + " not set");
    }
    final String queryUrl = prometheusManagementUrl + "/" + command;
    if (!apiHelper.postRequest(queryUrl)) {
      throw new RuntimeException(
          "Failed to perform " + command + " on prometheus instance " + prometheusManagementUrl);
    }
  }
}
