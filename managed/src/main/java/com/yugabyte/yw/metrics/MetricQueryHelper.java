// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;


import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiHelper;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.libs.Json;
import play.libs.Yaml;

import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Singleton
public class MetricQueryHelper {
  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryHelper.class);
  public static final Integer STEP_SIZE =  100;

  private ConcurrentHashMap<String, MetricConfig> metricsConfig = new ConcurrentHashMap<String, MetricConfig>();

  @Inject
  ApiHelper apiHelper;

  @Inject
  Configuration appConfig;

  /**
   * Query prometheus for a given metricType and query params
   * @param params, Query params like start, end timestamps, even filters
   *                Ex: {"metricKey": "cpu_usage_user",
   *                     "start": <start timestamp>,
   *                     "end": <end timestamp>}
   * @return MetricQueryResponse Object
   */
  public JsonNode query(Map<String, String> params) {
    String metricKey = params.remove("metricKey");
    MetricConfig metricConfig = getConfig(metricKey);

    if (metricConfig == null) {
      LOG.error("Invalid metric key: " + metricKey);
      throw new RuntimeException("Invalid metric key: " + metricKey);
    }

    String queryUrl;
    Long timeDifference;
    if (params.get("end") != null) {
      queryUrl =  getMetricsUrl() + "/query_range";
      timeDifference = Long.parseLong(params.get("end")) - Long.parseLong(params.get("start"));
    } else {
      queryUrl = getMetricsUrl() + "/query";
      String startTime = params.remove("start").toString();
      Integer endTime = Math.round(DateTime.now().getMillis()/1000);
      params.put("time", startTime);
      params.put("_", endTime.toString());
      timeDifference = endTime - Long.parseLong(startTime);
    }

    Integer resolution = Math.round(timeDifference / STEP_SIZE);
    params.put("step", resolution.toString());

    HashMap<String, String> filters = new HashMap<String, String>();
    if (params.containsKey("filters")) {
      try {
        filters = new ObjectMapper().readValue(params.get("filters"), HashMap.class);
      } catch (IOException e) {
        throw new RuntimeException("Invalid filter params provided, it should be a hash");
      }
    }
    params.put("query", metricConfig.getQuery(filters));
    LOG.info("Executing metric query {}: {}", queryUrl, params);
    JsonNode response = apiHelper.getRequest(queryUrl, new HashMap<String, String>(), params);
    MetricQueryResponse metricQueryResponse = Json.fromJson(response, MetricQueryResponse.class);
    if (metricQueryResponse.error != null) {
      return Json.toJson(metricQueryResponse);
    }

    ObjectNode responseJson = Json.newObject();
    responseJson.put("metricKey", metricKey);
    responseJson.set("data", getMetricsGraphData(metricQueryResponse));
    responseJson.set("layout", Json.toJson(metricConfig.layout));
    return responseJson;
  }

  /**
   * Loads the metrics from the yaml file, if it is empty, and returns the metrics config
   * for the given metric type
   * @param metricType, metric type
   * @return MetricConfig returns a metric config object
   */
  private MetricConfig getConfig(String metricType) {
    if (metricsConfig.isEmpty()) {
      ObjectMapper mapper = new ObjectMapper();
      Map<String, Object> configs = (HashMap<String, Object>) Yaml.load("metrics.yml");
      for(Map.Entry<String, Object> config : configs.entrySet()) {
        metricsConfig.putIfAbsent(config.getKey(), mapper.convertValue(config.getValue(), MetricConfig.class));
      }
    }
    return metricsConfig.get(metricType);
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

  /**
   * Get the MetricQueryResponse object formatted as a json for graph(plot.ly) consumes
   * @param response, MetricQueryResponse object
   * @return JsonNode, Json data that plot.ly can understand
   */
  private JsonNode getMetricsGraphData(MetricQueryResponse response) {
    ArrayList<MetricGraphData> metricGraphDataList = new ArrayList<>();
    SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");

    for (final JsonNode objNode : response.data.result) {
      MetricGraphData metricGraphData = new MetricGraphData();
      JsonNode metricInfo = objNode.get("metric");

      if (metricInfo.has("exported_instance")) {
        metricGraphData.name = metricInfo.get("exported_instance").asText();
      } else if (metricInfo.has("node_prefix")) {
        metricGraphData.name = metricInfo.get("node_prefix").asText();
      } else if (metricInfo.size() == 1) {
        // If we have a group_by clause, the group by name would be the only
        // key in the metrics data, fetch that and use that as the name
        String key = metricInfo.fieldNames().next();
        metricGraphData.name = metricInfo.get(key).asText();
      }

      if (objNode.has("values")) {
        for (final JsonNode valueNode: objNode.get("values")) {
          metricGraphData.x.add(dateFormat.format(valueNode.get(0).asLong() * 1000));
          metricGraphData.y.add(valueNode.get(1));
        }
      } else if (objNode.has("value")) {
        metricGraphData.x.add(dateFormat.format(objNode.get("value").get(0).asLong() * 1000));
        metricGraphData.y.add(objNode.get("value").get(1));
      }
      metricGraphData.type = "scatter";
      metricGraphDataList.add(metricGraphData);
    }
    return Json.toJson(metricGraphDataList);
  }
}
