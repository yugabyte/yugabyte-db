// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.models.MetricConfig;
import play.libs.Json;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.HashMap;

public class MetricQueryResponse {
  public static class MetricsData {
    public String resultType;
    public ArrayNode result;
  }
  public String status;
  public MetricsData data;
  public String errorType;
  public String error;


  /**
   * Format MetricQueryResponse object as a json for graph(plot.ly) consumption.
   * @param layout, MetricConfig.Layout object
   * @return JsonNode, Json data that plot.ly can understand
   */
  public JsonNode getGraphData(MetricConfig.Layout layout) {
    ArrayList<MetricGraphData> metricGraphDataList = new ArrayList<>();
    SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");

    for (final JsonNode objNode : data.result) {
      MetricGraphData metricGraphData = new MetricGraphData();
      JsonNode metricInfo = objNode.get("metric");

      if (metricInfo.has("node_prefix")) {
        metricGraphData.name = metricInfo.get("node_prefix").asText();
      } else if (metricInfo.size() == 1) {
        // If we have a group_by clause, the group by name would be the only
        // key in the metrics data, fetch that and use that as the name
        String key = metricInfo.fieldNames().next();
        metricGraphData.name = metricInfo.get(key).asText();
        if (layout.yaxis != null && layout.yaxis.alias.containsKey(metricGraphData.name)) {
          metricGraphData.name = layout.yaxis.alias.get(metricGraphData.name);
        }
      }

      if (metricInfo.size() > 1) {
        metricGraphData.labels = new HashMap<String, String>();
        metricInfo.fields().forEachRemaining(handler -> {
          metricGraphData.labels.put(handler.getKey(), handler.getValue().asText());
        });
      }

      if (objNode.has("values")) {
        for (final JsonNode valueNode: objNode.get("values")) {
          metricGraphData.x.add(valueNode.get(0).asLong() * 1000);
          metricGraphData.y.add(valueNode.get(1));
        }
      } else if (objNode.has("value")) {
        metricGraphData.x.add(objNode.get("value").get(0).asLong() * 1000);
        metricGraphData.y.add(objNode.get("value").get(1));
      }
      metricGraphData.type = "scatter";
      metricGraphDataList.add(metricGraphData);
    }
    return Json.toJson(metricGraphDataList);
  }
}
