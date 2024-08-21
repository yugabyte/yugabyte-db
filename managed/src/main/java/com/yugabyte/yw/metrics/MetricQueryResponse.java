// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import static com.yugabyte.yw.metrics.MetricQueryHelper.EXPORTED_INSTANCE;
import static com.yugabyte.yw.metrics.MetricQueryHelper.NAMESPACE_ID;
import static com.yugabyte.yw.metrics.MetricQueryHelper.NAMESPACE_NAME;
import static com.yugabyte.yw.metrics.MetricQueryHelper.TABLE_ID;
import static com.yugabyte.yw.metrics.MetricQueryHelper.TABLE_NAME;
import static com.yugabyte.yw.metrics.MetricQueryHelper.YBA_INSTANCE_ADDRESS;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.MetricConfigDefinition;
import com.yugabyte.yw.models.MetricConfigDefinition.Layout;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class MetricQueryResponse {
  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryResponse.class);

  public static class MetricsData {
    public String resultType;
    public ArrayNode result;
  }

  public String status;
  public MetricsData data;
  public String errorType;
  public String error;

  public static class Entry {
    public HashMap<String, String> labels;
    public ArrayList<ImmutablePair<Double, Double>> values;

    public String toString() {
      ObjectMapper objMapper = new ObjectMapper();
      try {
        return objMapper.writeValueAsString(this);
      } catch (JsonProcessingException je) {
        LOG.error("Invalid object", je);
        return "ResultEntry: [invalid]";
      }
    }
  }

  /**
   * Format MetricQueryResponse object as a json for graph(plot.ly) consumption.
   *
   * @param metricName, metric name
   * @param config, metric definition
   * @param metricSettings, metric query settings
   * @return JsonNode, Json data that plot.ly can understand
   */
  public List<MetricGraphData> getGraphData(
      String metricName, MetricConfigDefinition config, MetricSettings metricSettings) {
    List<MetricGraphData> metricGraphDataList = new ArrayList<>();

    Layout layout = config.getLayout();
    // We should use instance name for aggregated graph in case it's grouped by instance.
    boolean useInstanceName =
        config.getGroupBy() != null
            && config.getGroupBy().equals(EXPORTED_INSTANCE)
            && metricSettings.getSplitMode() == SplitMode.NONE;
    for (final JsonNode objNode : data.result) {
      MetricGraphData metricGraphData = new MetricGraphData();
      ObjectNode metricInfo = (ObjectNode) objNode.get("metric");

      metricGraphData.metricName = metricName;
      metricGraphData.instanceName = getAndRemoveLabelValue(metricInfo, EXPORTED_INSTANCE);
      metricGraphData.tableId = getAndRemoveLabelValue(metricInfo, TABLE_ID);
      metricGraphData.tableName = getAndRemoveLabelValue(metricInfo, TABLE_NAME);
      metricGraphData.namespaceName = getAndRemoveLabelValue(metricInfo, NAMESPACE_NAME);
      metricGraphData.namespaceId = getAndRemoveLabelValue(metricInfo, NAMESPACE_ID);
      metricGraphData.ybaInstanceAddress = getAndRemoveLabelValue(metricInfo, YBA_INSTANCE_ADDRESS);

      if (metricInfo.has("node_prefix")) {
        metricGraphData.name = metricInfo.get("node_prefix").asText();
      } else if (metricInfo.size() == 1) {
        // If we have a group_by clause, the group by name would be the only
        // key in the metrics data, fetch that and use that as the name
        String key = metricInfo.fieldNames().next();
        metricGraphData.name = metricInfo.get(key).asText();
      } else if (metricInfo.size() == 0) {
        if (useInstanceName && StringUtils.isNotBlank(metricGraphData.instanceName)) {
          // In case of aggregated metric query need to set name == instanceName for graphs,
          // which are grouped by instance name by default
          metricGraphData.name = metricGraphData.instanceName;
        } else {
          metricGraphData.name = metricName;
        }
      }

      if (metricInfo.size() <= 1) {
        if (layout.getYaxis() != null
            && layout.getYaxis().getAlias().containsKey(metricGraphData.name)) {
          metricGraphData.name = layout.getYaxis().getAlias().get(metricGraphData.name);
        }
      } else {
        metricGraphData.labels = new HashMap<>();
        // In case we want to use instance name - it's already set above
        // Otherwise - replace metric name with alias.
        if (layout.getYaxis() != null && !useInstanceName) {
          for (Map.Entry<String, String> entry : layout.getYaxis().getAlias().entrySet()) {
            boolean validLabels = false;
            for (String key : entry.getKey().split(",")) {
              validLabels = false;
              // Java conversion from Iterator to Iterable...
              for (JsonNode metricEntry : (Iterable<JsonNode>) metricInfo::elements) {
                if (metricEntry.asText().equals(key)) {
                  validLabels = true;
                  break;
                }
              }
              if (!validLabels) {
                break;
              }
            }
            if (validLabels) {
              metricGraphData.name = entry.getValue();
            }
          }
        } else {
          metricInfo
              .fields()
              .forEachRemaining(
                  handler -> {
                    metricGraphData.labels.put(handler.getKey(), handler.getValue().asText());
                  });
        }
      }
      if (objNode.has("values")) {
        for (final JsonNode valueNode : objNode.get("values")) {
          metricGraphData.x.add(valueNode.get(0).asLong() * 1000);
          JsonNode val = valueNode.get(1);
          if (val.asText().equals("NaN")) {
            metricGraphData.y.add(0);
          } else {
            metricGraphData.y.add(val);
          }
        }
      } else if (objNode.has("value")) {
        metricGraphData.x.add(objNode.get("value").get(0).asLong() * 1000);
        JsonNode val = objNode.get("value").get(1);
        if (val.asText().equals("NaN")) {
          metricGraphData.y.add(0);
        } else {
          metricGraphData.y.add(val);
        }
      }
      metricGraphData.type = "scatter";
      metricGraphDataList.add(metricGraphData);
    }
    return sortGraphData(metricGraphDataList, config);
  }

  private List<MetricGraphData> sortGraphData(
      List<MetricGraphData> graphData, MetricConfigDefinition configDefinition) {
    Map<String, Integer> nameOrderMap = new HashMap<>();
    if (configDefinition.getLayout().getYaxis() != null
        && configDefinition.getLayout().getYaxis().getAlias() != null) {
      int position = 1;
      for (String alias : configDefinition.getLayout().getYaxis().getAlias().values()) {
        nameOrderMap.put(alias, position++);
      }
    }
    return graphData.stream()
        .sorted(
            Comparator.comparing(
                data -> {
                  if (StringUtils.isEmpty(data.name)) {
                    return Integer.MAX_VALUE;
                  }
                  if (StringUtils.isEmpty(data.instanceName)
                      && StringUtils.isEmpty(data.namespaceName)) {
                    return Integer.MAX_VALUE;
                  }
                  Integer position = nameOrderMap.get(data.name);
                  if (position != null) {
                    return position;
                  }
                  return Integer.MAX_VALUE - 1;
                }))
        .collect(Collectors.toList());
  }

  private String getAndRemoveLabelValue(ObjectNode metricInfo, String labelName) {
    String value = null;
    if (metricInfo.has(labelName)) {
      value = metricInfo.get(labelName).asText();
      metricInfo.remove(labelName);
    }
    return value;
  }

  /**
   * Converts the JSON result of a prometheus HTTP query call to the MetricQueryResponse.Entry
   * format.
   */
  public ArrayList<MetricQueryResponse.Entry> getValues() {
    if (this.data == null || this.data.result == null) {
      return null;
    }
    ArrayList<MetricQueryResponse.Entry> result = new ArrayList<>();
    ObjectMapper objMapper = new ObjectMapper();
    for (final JsonNode entryNode : this.data.result) {
      try {
        final JsonNode metricNode = entryNode.get("metric");
        final JsonNode valueNode = entryNode.get("value");
        final JsonNode valuesNode = entryNode.get("values");
        if (metricNode == null || (valueNode == null && valuesNode == null)) {
          LOG.trace("Skipping json node while parsing prom response: {}", entryNode);
          continue;
        }
        MetricQueryResponse.Entry entry = new MetricQueryResponse.Entry();
        entry.labels = objMapper.convertValue(metricNode, HashMap.class);
        entry.values = new ArrayList<>();
        if (valueNode != null) {
          entry.values.add(
              new ImmutablePair<>(
                  Double.parseDouble(valueNode.get(0).asText()), // timestamp
                  Double.parseDouble(valueNode.get(1).asText()) // value
                  ));
        } else if (valuesNode != null) {
          entry.values = new ArrayList<>();
          Iterator<JsonNode> elements = valuesNode.elements();
          while (elements.hasNext()) {
            final JsonNode eachValueNode = elements.next();
            entry.values.add(
                new ImmutablePair<>(
                    Double.parseDouble(eachValueNode.get(0).asText()), // timestamp
                    Double.parseDouble(eachValueNode.get(1).asText()) // value
                    ));
          }
        }
        result.add(entry);
      } catch (Exception e) {
        LOG.debug("Skipping json node while parsing prometheus response: {}", entryNode, e);
      }
    }
    return result;
  }
}
