// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.MetricConfig;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TimeZone;
import java.util.concurrent.Callable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class MetricQueryExecutor implements Callable<JsonNode> {
  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryExecutor.class);
  public static final String DATE_FORMAT_STRING = "yyyy-MM-dd HH:mm:ss";
  private ApiHelper apiHelper;
  private play.Configuration appConfig;
  private YBMetricQueryComponent ybMetricQueryComponent;

  private Map<String, String> queryParam = new HashMap<>();
  private Map<String, String> additionalFilters = new HashMap<>();
  private String queryUrl;
  private int queryRangeSecs = 0;

  private boolean isRecharts;

  public MetricQueryExecutor(
      play.Configuration appConfig,
      ApiHelper apiHelper,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters,
      YBMetricQueryComponent ybMetricQueryComponent) {
    this(appConfig, apiHelper, queryParam, additionalFilters, ybMetricQueryComponent, false);
  }

  public MetricQueryExecutor(
      play.Configuration appConfig,
      ApiHelper apiHelper,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters,
      YBMetricQueryComponent ybMetricQueryComponent,
      boolean isRecharts) {
    this.apiHelper = apiHelper;
    this.appConfig = appConfig;
    this.queryParam.putAll(queryParam);
    this.additionalFilters.putAll(additionalFilters);
    this.ybMetricQueryComponent = ybMetricQueryComponent;
    this.isRecharts = isRecharts;
    int scrapeIntervalSecs = appConfig.getInt("yb.metrics.scrape_interval_secs", 10);
    if (queryParam.containsKey("step")) {
      // Rate queries like rate(rpc_latency_count[rate_interval]) are performed over multiple
      // windows of size "step" in the query range (start, end). We set rate_interval to the step
      // size so that the rate is computed over all points in the 'step' window.
      //
      // One minor issue here is that this approach does not include changes that happen at the
      // boundary of the step windows, so we add in 2 scrape intervals to calculate rate
      // including the boundary. This makes the rate smoother but also slightly inaccurate.
      // We use 2 scrape intervals instead of 1 because with the current low
      // scrape interval of 10s it could be that we don't have scrapes in the exact 10s interval.
      try {
        this.queryRangeSecs = Integer.parseInt(queryParam.get("step")) + 2 * scrapeIntervalSecs;
      } catch (NumberFormatException ex) {
        LOG.warn("Invalid value for step parameter, ignoring: " + queryParam.get("step"));
      }
    } else {
      LOG.warn(
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
    boolean useNativeMetrics = appConfig.getBoolean("yb.metrics.useNative", false);
    if (useNativeMetrics) {
      return ybMetricQueryComponent.query(queryParam);
    } else {
      if (queryParam.containsKey("end")) {
        this.queryUrl = this.getMetricsUrl() + "/query_range";
      } else {
        this.queryUrl = this.getMetricsUrl() + "/query";
      }

      LOG.trace("Executing metric query {}: {}", queryUrl, queryParam);
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
    MetricConfig config = MetricConfig.get(queryParam.get("queryKey"));
    ObjectNode responseJson = Json.newObject();
    responseJson.put("queryKey", queryParam.get("queryKey"));

    if (config == null) {
      responseJson.put("error", "Invalid Query Key");
    } else {
      Map<String, String> queries = config.getQueries(additionalFilters, this.queryRangeSecs);
      responseJson.set("layout", Json.toJson(config.getLayout()));
      MetricRechartsGraphData rechartsOutput = new MetricRechartsGraphData();
      List<MetricGraphData> output = new ArrayList<>();
      for (Map.Entry<String, String> e : queries.entrySet()) {
        String metric = e.getKey();
        String queryExpr = e.getValue();
        queryParam.put("query", queryExpr);
        try {
          responseJson.put("directURL", getDirectURL(queryExpr));
        } catch (Exception de) {
          LOG.trace("Error getting direct url", de);
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
}
