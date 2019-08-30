// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;


import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.MetricConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.libs.Json;

import java.util.HashMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;

public class MetricQueryExecutor implements Callable<JsonNode> {
  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryExecutor.class);

  private ApiHelper apiHelper;
  private Configuration appConfig;
  private YBMetricQueryComponent ybMetricQueryComponent;

  private Map<String, String> queryParam = new HashMap<>();
  private Map<String, String> additionalFilters = new HashMap<>();
  private String queryUrl;

  public MetricQueryExecutor(Configuration appConfig, ApiHelper apiHelper,
                             Map<String, String> queryParam, Map<String, String> additionalFilters,
                             YBMetricQueryComponent ybMetricQueryComponent) {
    this.apiHelper = apiHelper;
    this.appConfig = appConfig;
    this.queryParam.putAll(queryParam);
    this.additionalFilters.putAll(additionalFilters);
    this.ybMetricQueryComponent = ybMetricQueryComponent;

    // LOG.info("Executing metric query {}: {}", queryUrl, queryParam);
  }

  /**
   * Get the metrics base uri based on the appConfig yb.metrics.uri
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
      return apiHelper.getRequest(queryUrl, new HashMap<String, String>(), queryParam);
    }
  }

  @Override
  public JsonNode call() throws Exception {
    MetricConfig config = MetricConfig.get(queryParam.get("queryKey"));
    ObjectNode responseJson = Json.newObject();
    responseJson.put("queryKey", queryParam.get("queryKey"));

    if (config == null) {
      responseJson.put("error", "Invalid Query Key");
    } else {
      Map<String, String> queries = config.getQueries(additionalFilters);
      responseJson.set("layout", Json.toJson(config.getLayout()));
      ArrayList<MetricGraphData> output = new ArrayList<>();
      for (Map.Entry<String, String> e : queries.entrySet()) {
        String metric = e.getKey();
        queryParam.put("query", e.getValue());
        JsonNode queryResponseJson = getMetrics();
        if (queryResponseJson == null) {
          responseJson.set("data", Json.toJson(new ArrayList()));
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
      responseJson.set("data", Json.toJson(output));
    }
    return responseJson;
  }
}
