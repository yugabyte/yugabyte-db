// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.MetricConfig;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TimeZone;
import java.util.concurrent.Callable;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import net.logstash.logback.encoder.org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class MetricQueryExecutor implements Callable<JsonNode> {

  public static final String EXPORTED_INSTANCE = "exported_instance";
  public static final String DATE_FORMAT_STRING = "yyyy-MM-dd HH:mm:ss";
  private ApiHelper apiHelper;
  private play.Configuration appConfig;
  private YBMetricQueryComponent ybMetricQueryComponent;

  private Map<String, String> queryParam = new HashMap<>();
  private Map<String, String> additionalFilters = new HashMap<>();
  private String queryUrl;
  private int queryRangeSecs = 0;
  private MetricSettings metricSettings;

  private boolean isRecharts;

  public MetricQueryExecutor(
      play.Configuration appConfig,
      ApiHelper apiHelper,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters,
      YBMetricQueryComponent ybMetricQueryComponent) {
    this(
        appConfig,
        apiHelper,
        queryParam,
        additionalFilters,
        ybMetricQueryComponent,
        MetricSettings.defaultSettings(queryParam.get("queryKey")),
        false);
  }

  public MetricQueryExecutor(
      play.Configuration appConfig,
      ApiHelper apiHelper,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters,
      YBMetricQueryComponent ybMetricQueryComponent,
      MetricSettings metricSettings,
      boolean isRecharts) {
    this.apiHelper = apiHelper;
    this.appConfig = appConfig;
    this.queryParam.putAll(queryParam);
    this.additionalFilters.putAll(additionalFilters);
    this.ybMetricQueryComponent = ybMetricQueryComponent;
    this.metricSettings = metricSettings;
    this.isRecharts = isRecharts;
    if (queryParam.containsKey("step")) {
      this.queryRangeSecs = Integer.parseInt(queryParam.get("step"));
    } else {
      log.warn(
          "Missing step size in query parameters, this is unexpected. "
              + "Queries over longer time windows like 6h/1d will be inaccurate.");
    }
  }

  /**
   * Get the metrics base uri based on the appConfig yb.metrics.uri
   *
   * @return returns metrics url string
   */
  private String getMetricsUrl() {
    String metricsUrl = appConfig.getString("yb.metrics.url");
    if (metricsUrl == null || metricsUrl.isEmpty()) {
      throw new RuntimeException("yb.metrics.url not set");
    }

    return metricsUrl;
  }

  private JsonNode getMetrics() {
    return getMetrics(this.queryParam);
  }

  private JsonNode getMetrics(Map<String, String> queryParam) {
    boolean useNativeMetrics = appConfig.getBoolean("yb.metrics.useNative", false);
    if (useNativeMetrics) {
      return ybMetricQueryComponent.query(queryParam);
    } else {
      if (queryParam.containsKey("end")) {
        this.queryUrl = this.getMetricsUrl() + "/query_range";
      } else {
        this.queryUrl = this.getMetricsUrl() + "/query";
      }

      log.trace("Executing metric query {}: {}", queryUrl, queryParam);
      return apiHelper.getRequest(queryUrl, new HashMap<>(), queryParam);
    }
  }

  private String getDirectURL(String queryExpr) {

    String durationSecs = "3600s";
    String endString = "";

    long endUnixTime = Long.parseLong(queryParam.getOrDefault("end", "0"));
    long startUnixTime = Long.parseLong(queryParam.getOrDefault("start", "0"));
    if (endUnixTime != 0 && startUnixTime != 0 && endUnixTime > startUnixTime) {
      // The timezone is set to UTC because If there is a discrepancy between platform and
      // prometheus timezones, the resulting directURL will show incorrect timeframe.
      endString =
          Util.unixTimeToDateString(
              endUnixTime * 1000, DATE_FORMAT_STRING, TimeZone.getTimeZone("UTC"));
      durationSecs = String.format("%ds", (endUnixTime - startUnixTime));
    }

    // Note: this is the URL as prometheus' web interface renders these metrics. It is
    // possible this breaks over time as we upgrade prometheus.
    return String.format(
        "%s/graph?g0.expr=%s&g0.tab=0&g0.range_input=%s&g0.end_input=%s",
        this.getMetricsUrl().replace("/api/v1", ""),
        URLEncoder.encode(queryExpr),
        durationSecs,
        endString);
  }

  @Override
  public JsonNode call() {
    String metricName = queryParam.get("queryKey");
    MetricConfig config = MetricConfig.get(metricName);
    ObjectNode responseJson = Json.newObject();
    responseJson.put("queryKey", metricName);

    if (config == null) {
      responseJson.put("error", "Invalid Query Key");
    } else {
      Map<String, List<MetricLabelFilters>> topNodeFilters;
      try {
        topNodeFilters = getTopNodesFilters(config, responseJson);
      } catch (Exception e) {
        log.error("Error occurred getting top nodes list for metric" + metricName, e);
        responseJson.put("error", e.getMessage());
        return responseJson;
      }

      MetricQueryContext context =
          MetricQueryContext.builder()
              .queryRangeSecs(queryRangeSecs)
              .additionalFilters(additionalFilters)
              .metricOrFilters(topNodeFilters)
              .additionalGroupBy(
                  metricSettings.splitTopNodes > 0
                      ? ImmutableSet.of(EXPORTED_INSTANCE)
                      : Collections.emptySet())
              .build();
      Map<String, String> queries = config.getQueries(this.metricSettings, context);
      responseJson.set("layout", Json.toJson(config.getLayout()));
      MetricRechartsGraphData rechartsOutput = new MetricRechartsGraphData();
      List<MetricGraphData> output = new ArrayList<>();
      ArrayNode directURLs = responseJson.putArray("directURLs");
      for (Map.Entry<String, String> e : queries.entrySet()) {
        String metric = e.getKey();
        String queryExpr = e.getValue();
        queryParam.put("query", queryExpr);
        try {
          directURLs.add(getDirectURL(queryExpr));
        } catch (Exception de) {
          log.trace("Error getting direct url", de);
        }
        JsonNode queryResponseJson = getMetrics();
        if (queryResponseJson == null) {
          responseJson.set("data", Json.toJson(new ArrayList<>()));

          return responseJson;
        }
        MetricQueryResponse queryResponse =
            Json.fromJson(queryResponseJson, MetricQueryResponse.class);
        if (queryResponse.error != null) {
          responseJson.put("error", queryResponse.error);
          break;
        } else {
          output.addAll(queryResponse.getGraphData(metric, config.getLayout()));
        }
      }
      if (isRecharts) {
        responseJson.set("data", Json.toJson(rechartsOutput));
      } else {
        responseJson.set("data", Json.toJson(output));
      }
    }

    return responseJson;
  }

  private Map<String, List<MetricLabelFilters>> getTopNodesFilters(
      MetricConfig config, ObjectNode responseJson) {
    if (metricSettings.getSplitTopNodes() <= 0) {
      return Collections.emptyMap();
    }
    int range = Integer.parseInt(queryParam.get("range"));
    MetricQueryContext context =
        MetricQueryContext.builder()
            .topKQuery(true)
            .queryRangeSecs(range)
            .additionalFilters(additionalFilters)
            .additionalGroupBy(ImmutableSet.of(EXPORTED_INSTANCE))
            .build();
    Map<String, String> queries = config.getQueries(this.metricSettings, context);
    Map<String, String> topKQueryParams = new HashMap<>(queryParam);
    String endTime = topKQueryParams.remove("end");
    if (StringUtils.isNotBlank(endTime)) {
      // 'time' param for top K query is equal to metric query period end.
      topKQueryParams.put("time", endTime);
    }
    Map<String, List<MetricLabelFilters>> results = new HashMap<>();
    ArrayNode topNodesQueryURLs = responseJson.putArray("topNodesQueryURLs");
    for (Map.Entry<String, String> e : queries.entrySet()) {
      String metric = e.getKey();
      String queryExpr = e.getValue();
      topKQueryParams.put("query", queryExpr);

      JsonNode queryResponseJson = getMetrics(topKQueryParams);
      MetricQueryResponse queryResponse =
          Json.fromJson(queryResponseJson, MetricQueryResponse.class);

      try {
        topNodesQueryURLs.add(getDirectURL(queryExpr));
      } catch (Exception de) {
        log.trace("Error getting direct url", de);
      }

      if (queryResponse.error != null) {
        throw new RuntimeException("Failed to get top nodes: " + queryResponse.error);
      }
      List<MetricLabelFilters> metricLabelFilters =
          queryResponse
              .getValues()
              .stream()
              .map(
                  entry ->
                      MetricLabelFilters.builder()
                          .filters(
                              entry
                                  .labels
                                  .entrySet()
                                  .stream()
                                  .map(
                                      label ->
                                          new MetricLabelFilter(label.getKey(), label.getValue()))
                                  .collect(Collectors.toList()))
                          .build())
              .collect(Collectors.toList());
      results.put(metric, metricLabelFilters);
    }
    return results;
  }
}
