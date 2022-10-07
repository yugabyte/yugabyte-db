// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.MetricConfigDefinition;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import net.logstash.logback.encoder.org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class MetricQueryExecutor implements Callable<JsonNode> {

  public static final String DATE_FORMAT_STRING = "yyyy-MM-dd HH:mm:ss";
  private final ApiHelper apiHelper;

  private final MetricUrlProvider metricUrlProvider;

  private final Map<String, String> queryParam = new HashMap<>();
  private final Map<String, String> additionalFilters = new HashMap<>();
  private int queryRangeSecs = 0;
  private final MetricSettings metricSettings;

  private final boolean isRecharts;

  public MetricQueryExecutor(
      MetricUrlProvider metricUrlProvider,
      ApiHelper apiHelper,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters) {
    this(
        metricUrlProvider,
        apiHelper,
        queryParam,
        additionalFilters,
        MetricSettings.defaultSettings(queryParam.get("queryKey")),
        false);
  }

  public MetricQueryExecutor(
      MetricUrlProvider metricUrlProvider,
      ApiHelper apiHelper,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters,
      MetricSettings metricSettings,
      boolean isRecharts) {
    this.apiHelper = apiHelper;
    this.metricUrlProvider = metricUrlProvider;
    this.queryParam.putAll(queryParam);
    this.additionalFilters.putAll(additionalFilters);
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

  private JsonNode getMetrics() {
    return getMetrics(this.queryParam);
  }

  private JsonNode getMetrics(Map<String, String> queryParam) {
    String queryUrl;
    if (queryParam.containsKey("end")) {
      queryUrl = metricUrlProvider.getMetricsUrl() + "/query_range";
    } else {
      queryUrl = metricUrlProvider.getMetricsUrl() + "/query";
    }

    log.trace("Executing metric query {}: {}", queryUrl, queryParam);
    return apiHelper.getRequest(queryUrl, new HashMap<>(), queryParam);
  }

  private String getDirectURL(String queryExpr) {
    long endUnixTime = Long.parseLong(queryParam.getOrDefault("end", "0"));
    long startUnixTime = Long.parseLong(queryParam.getOrDefault("start", "0"));

    return metricUrlProvider.getExpressionUrl(queryExpr, startUnixTime, endUnixTime);
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
      MetricConfigDefinition configDefinition = config.getConfig();
      Map<String, List<MetricLabelFilters>> splitQueryFilters;
      try {
        splitQueryFilters = getSplitQueryFilters(configDefinition, responseJson);
      } catch (Exception e) {
        log.error("Error occurred split query filters list for metric" + metricName, e);
        responseJson.put("error", e.getMessage());
        return responseJson;
      }

      MetricQueryContext context =
          MetricQueryContext.builder()
              .queryRangeSecs(queryRangeSecs)
              .additionalFilters(additionalFilters)
              .metricOrFilters(splitQueryFilters)
              .additionalGroupBy(getAdditionalGroupBy(metricSettings))
              .build();
      Map<String, String> queries = configDefinition.getQueries(this.metricSettings, context);
      responseJson.set("layout", Json.toJson(configDefinition.getLayout()));
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
          output.addAll(queryResponse.getGraphData(metric, configDefinition, metricSettings));
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

  private Map<String, List<MetricLabelFilters>> getSplitQueryFilters(
      MetricConfigDefinition configDefinition, ObjectNode responseJson) {
    if (metricSettings.getSplitMode() == SplitMode.NONE) {
      return Collections.emptyMap();
    }
    int range = Integer.parseInt(queryParam.get("range"));
    MetricQueryContext context =
        MetricQueryContext.builder()
            .topKQuery(true)
            .queryRangeSecs(range)
            .additionalFilters(additionalFilters)
            .additionalGroupBy(getAdditionalGroupBy(metricSettings))
            .build();
    Map<String, String> queries = configDefinition.getQueries(this.metricSettings, context);
    Map<String, String> topKQueryParams = new HashMap<>(queryParam);
    String endTime = topKQueryParams.remove("end");
    if (StringUtils.isNotBlank(endTime)) {
      // 'time' param for top K query is equal to metric query period end.
      topKQueryParams.put("time", endTime);
    }
    Map<String, List<MetricLabelFilters>> results = new HashMap<>();
    ArrayNode topKQueryURLs = responseJson.putArray("topKQueryURLs");
    for (Map.Entry<String, String> e : queries.entrySet()) {
      String metric = e.getKey();
      String queryExpr = e.getValue();
      topKQueryParams.put("query", queryExpr);

      JsonNode queryResponseJson = getMetrics(topKQueryParams);
      MetricQueryResponse queryResponse =
          Json.fromJson(queryResponseJson, MetricQueryResponse.class);

      try {
        topKQueryURLs.add(getDirectURL(queryExpr));
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

  private Set<String> getAdditionalGroupBy(MetricSettings metricSettings) {
    switch (metricSettings.getSplitType()) {
      case NODE:
        return ImmutableSet.of(MetricQueryHelper.EXPORTED_INSTANCE);
      case TABLE:
        return ImmutableSet.of(MetricQueryHelper.TABLE_ID, MetricQueryHelper.TABLE_NAME);
      case NAMESPACE:
        return ImmutableSet.of(MetricQueryHelper.NAMESPACE_NAME);
      default:
        return Collections.emptySet();
    }
  }
}
