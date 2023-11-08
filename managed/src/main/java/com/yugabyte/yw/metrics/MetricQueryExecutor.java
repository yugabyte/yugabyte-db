// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;

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
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class MetricQueryExecutor implements Callable<JsonNode> {
  private final ApiHelper apiHelper;

  private final MetricUrlProvider metricUrlProvider;

  private final Map<String, String> headers = new HashMap<>();
  private final Map<String, String> queryParam = new HashMap<>();
  private final Map<String, String> additionalFilters = new HashMap<>();
  private int queryRangeSecs = 0;
  private final MetricSettings metricSettings;

  private final boolean isRecharts;

  public MetricQueryExecutor(
      MetricUrlProvider metricUrlProvider,
      ApiHelper apiHelper,
      Map<String, String> headers,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters) {
    this(
        metricUrlProvider,
        apiHelper,
        headers,
        queryParam,
        additionalFilters,
        MetricSettings.defaultSettings(queryParam.get("queryKey")),
        false);
  }

  public MetricQueryExecutor(
      MetricUrlProvider metricUrlProvider,
      ApiHelper apiHelper,
      Map<String, String> headers,
      Map<String, String> queryParam,
      Map<String, String> additionalFilters,
      MetricSettings metricSettings,
      boolean isRecharts) {
    this.apiHelper = apiHelper;
    this.metricUrlProvider = metricUrlProvider;
    this.headers.putAll(headers);
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
      queryUrl = metricUrlProvider.getMetricsApiUrl() + "/query_range";
    } else {
      queryUrl = metricUrlProvider.getMetricsApiUrl() + "/query";
    }

    log.trace("Executing metric query {}: {}", queryUrl, queryParam);
    return apiHelper.getRequest(queryUrl, headers, queryParam);
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
      Map<String, String> topKQueries = Collections.emptyMap();
      Map<String, String> aggregatedQueries = Collections.emptyMap();
      MetricQueryContext context =
          MetricQueryContext.builder()
              .queryRangeSecs(queryRangeSecs)
              .additionalFilters(additionalFilters)
              .additionalGroupBy(getAdditionalGroupBy(metricSettings))
              .excludeFilters(getExcludeFilters(metricSettings))
              .build();
      if (metricSettings.getSplitMode() != SplitMode.NONE) {
        try {
          topKQueries = getTopKQueries(configDefinition, responseJson);
        } catch (Exception e) {
          log.error("Error while generating top K queries for " + metricName, e);
          responseJson.put("error", e.getMessage());
          return responseJson;
        }
        if (metricSettings.isReturnAggregatedValue()) {
          try {
            MetricQueryContext aggregatedContext =
                context.toBuilder().secondLevelAggregation(true).build();
            aggregatedQueries = configDefinition.getQueries(this.metricSettings, aggregatedContext);
          } catch (Exception e) {
            log.error("Error while generating aggregated queries for " + metricName, e);
            responseJson.put("error", e.getMessage());
            return responseJson;
          }
        }
      }

      Map<String, String> queries = configDefinition.getQueries(this.metricSettings, context);
      responseJson.set("layout", Json.toJson(configDefinition.getLayout()));
      MetricRechartsGraphData rechartsOutput = new MetricRechartsGraphData();
      List<MetricGraphData> output = new ArrayList<>();
      ArrayNode directURLs = responseJson.putArray("directURLs");
      responseJson.put(
          "metricsLinkUseBrowserFqdn", metricUrlProvider.getMetricsLinkUseBrowserFqdn());
      for (Map.Entry<String, String> e : queries.entrySet()) {
        String metric = e.getKey();
        String queryExpr = e.getValue();
        String topKQuery = topKQueries.get(metric);
        if (!StringUtils.isEmpty(topKQuery)) {
          queryExpr += " and " + topKQuery;
        }
        String aggregatedQuery = aggregatedQueries.get(metric);
        if (!StringUtils.isEmpty(aggregatedQuery)) {
          queryExpr = "(" + queryExpr + ") or " + aggregatedQuery;
        }
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

  private Map<String, String> getTopKQueries(
      MetricConfigDefinition configDefinition, ObjectNode responseJson) {
    if (metricSettings.getSplitMode() == SplitMode.NONE) {
      return Collections.emptyMap();
    }
    int range = Integer.parseInt(queryParam.get("range"));
    long end = Integer.parseInt(queryParam.get("end"));
    MetricQueryContext context =
        MetricQueryContext.builder()
            .topKQuery(true)
            .queryRangeSecs(range)
            .queryTimestampSec(end)
            .additionalFilters(additionalFilters)
            .additionalGroupBy(getAdditionalGroupBy(metricSettings))
            .excludeFilters(getExcludeFilters(metricSettings))
            .build();
    return configDefinition.getQueries(this.metricSettings, context);
  }

  public static Set<String> getAdditionalGroupBy(MetricSettings metricSettings) {
    switch (metricSettings.getSplitType()) {
      case NODE:
        return ImmutableSet.of(MetricQueryHelper.EXPORTED_INSTANCE);
      case TABLE:
        return ImmutableSet.of(
            MetricQueryHelper.NAMESPACE_NAME,
            MetricQueryHelper.TABLE_ID,
            MetricQueryHelper.TABLE_NAME);
      case NAMESPACE:
        return ImmutableSet.of(MetricQueryHelper.NAMESPACE_NAME);
      default:
        return Collections.emptySet();
    }
  }

  private Map<String, String> getExcludeFilters(MetricSettings metricSettings) {
    switch (metricSettings.getSplitType()) {
      case TABLE:
      case NAMESPACE:
        return Collections.singletonMap(MetricQueryHelper.NAMESPACE_NAME, SYSTEM_PLATFORM_DB);
      default:
        return Collections.emptyMap();
    }
  }
}
