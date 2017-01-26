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
import java.util.Map;
import java.util.concurrent.Callable;

public class MetricQueryExecutor implements Callable<JsonNode> {
  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryExecutor.class);

  private ApiHelper apiHelper;
  private Configuration appConfig;

  private Map<String, String> queryParam = new HashMap<>();
  private Map<String, String> additionalFilters = new HashMap<>();
  private String queryUrl;

  public MetricQueryExecutor(Configuration appConfig, ApiHelper apiHelper,
                             Map<String, String> queryParam, Map<String, String> additionalFilters) {
    this.apiHelper = apiHelper;
    this.appConfig = appConfig;
    this.queryParam.putAll(queryParam);
    this.additionalFilters.putAll(additionalFilters);

    if (queryParam.containsKey("end")) {
      this.queryUrl = this.getMetricsUrl() + "/query_range";
    } else {
      this.queryUrl = this.getMetricsUrl() + "/query";
    }

    LOG.info("Executing metric query {}: {}", queryUrl, queryParam);
  }

  /**
   * Get the metrics base uri based on the appConfig yb.metrics.uri
   * @return returns metrics url string
   */
  private String getMetricsUrl() {
    String metricsUrl = appConfig.getString("yb.metrics.url");
    if (metricsUrl == null) {
      throw new RuntimeException("yb.metrics.url not set");
    }
    return metricsUrl;
  }

  @Override
  public JsonNode call() throws Exception {
    MetricConfig config = MetricConfig.get(queryParam.get("queryKey"));
    ObjectNode responseJson = Json.newObject();
    responseJson.put("queryKey", queryParam.get("queryKey"));

    if (config == null) {
      responseJson.put("error", "Invalid Query Key");
    } else {
      queryParam.put("query", config.getQuery(additionalFilters));
      responseJson.set("layout", Json.toJson(config.getLayout()));
      JsonNode queryResponseJson =
        apiHelper.getRequest(queryUrl, new HashMap<String, String>(), queryParam);
      MetricQueryResponse queryResponse =
        Json.fromJson(queryResponseJson, MetricQueryResponse.class);

      if (queryResponse.error != null) {
        responseJson.put("error", queryResponse.error);
      } else {
        responseJson.set("data", queryResponse.getGraphData(config.getLayout()));
      }
    }
    return responseJson;
  }
}
