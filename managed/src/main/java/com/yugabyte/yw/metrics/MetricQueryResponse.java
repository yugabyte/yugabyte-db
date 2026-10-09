// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.metrics;

import static com.yugabyte.yw.metrics.MetricQueryHelper.AZ_NAME;
import static com.yugabyte.yw.metrics.MetricQueryHelper.CONTAINER_METRIC_PREFIX;
import static com.yugabyte.yw.metrics.MetricQueryHelper.CONTAINER_VOLUME_METRIC_PREFIX;
import static com.yugabyte.yw.metrics.MetricQueryHelper.EXPORTED_INSTANCE;
import static com.yugabyte.yw.metrics.MetricQueryHelper.KUBELET_VOLUME_METRIC_PREFIX;
import static com.yugabyte.yw.metrics.MetricQueryHelper.NAMESPACE;
import static com.yugabyte.yw.metrics.MetricQueryHelper.NAMESPACE_ID;
import static com.yugabyte.yw.metrics.MetricQueryHelper.NAMESPACE_NAME;
import static com.yugabyte.yw.metrics.MetricQueryHelper.POD_NAME;
import static com.yugabyte.yw.metrics.MetricQueryHelper.PVC;
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
import java.util.regex.Matcher;
import java.util.regex.Pattern;
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
   * <p>Convenience overload used by call sites without universe context; skips the resolver-based
   * lookup entirely.
   */
  public List<MetricGraphData> getGraphData(
      String metricName, MetricConfigDefinition config, MetricSettings metricSettings) {
    return getGraphData(metricName, config, metricSettings, null);
  }

  /**
   * Format MetricQueryResponse object as a json for graph(plot.ly) consumption.
   *
   * @param metricName metric name
   * @param config metric definition
   * @param metricSettings metric query settings
   * @param k8sPodNodeNameResolver resolver keyed off cluster / helm-release metadata rather than
   *     live NodeDetails, so it also handles historical samples whose pods have since been removed.
   *     When non-null the resolved node name is used verbatim, which correctly encodes AZ suffix
   *     for multi-AZ universes and {@code -readonly} for read-replica clusters. Also carries the
   *     universe-level multi-AZ flag used by the fallback path when the resolver can't identify a
   *     particular pod. Callers without universe context can pass {@code null}, in which case the
   *     fallback path defaults to multi-AZ naming for backward compatibility.
   * @return JsonNode, Json data that plot.ly can understand
   */
  public List<MetricGraphData> getGraphData(
      String metricName,
      MetricConfigDefinition config,
      MetricSettings metricSettings,
      K8sPodNodeNameResolver k8sPodNodeNameResolver) {
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
      // For container metrics, also check for pod_name label to set instanceName
      if (metricGraphData.instanceName == null
          && (metricName.startsWith(CONTAINER_METRIC_PREFIX)
              || metricName.startsWith(KUBELET_VOLUME_METRIC_PREFIX))) {
        metricGraphData.instanceName =
            getInstanceNameLabelForKubernetes(metricInfo, metricName, k8sPodNodeNameResolver);
      }
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

  // PVC names for yb-master/yb-tserver pods are formed by Kubernetes as
  // `<volumeClaimTemplateName>-<podName>`. In the yugabyte Helm chart:
  //   * volumeClaimTemplate name is `datadir<N>` in old naming style, or
  //     `<fullname>datadir<N>` in new naming style (where `<fullname>` is the helm-release-
  //     derived prefix like `ybamalyshev-single-pdmc-`);
  //   * pod name is `yb-<server>-<idx>` in old naming style, or `<fullname>yb-<server>-<idx>`
  //     in new naming style.
  // So actual PVC labels look like `datadir0-yb-tserver-0` (old) or
  // `ybamalyshev-single-pdmc-datadir0-ybamalyshev-single-pdmc-yb-tserver-0` (new). We use a
  // non-greedy leading `.*?` to accept the new-style fullname prefix without accidentally
  // capturing later `datadir<N>` occurrences (there is only one per PVC, at the boundary
  // between the volume-claim-template name and the pod name).
  private static final Pattern PVC_DATADIR_POD_PATTERN = Pattern.compile("^.*?(datadir\\d+)-(.+)$");

  private String getInstanceNameLabelForKubernetes(
      ObjectNode metricInfo, String metricName, K8sPodNodeNameResolver k8sPodNodeNameResolver) {
    // Read the namespace before removing it - the resolver uses it as a fallback disambiguator
    // for old-naming-style universes where the pod name doesn't embed the helm release. We
    // still strip the label from `metricInfo` (as before) so it doesn't leak into the graph's
    // `labels` map.
    String namespaceLabel = getAndRemoveLabelValue(metricInfo, NAMESPACE);
    // The AZ label is always emitted by Prometheus but is only part of the YBA node name when the
    // universe spans multiple AZs. Strip it unconditionally so it doesn't leak into `labels`;
    // we decide separately (below) whether to fold it into the instance name.
    String azNameLabel = getAndRemoveLabelValue(metricInfo, AZ_NAME);

    boolean isVolumeMetric =
        metricName.startsWith(CONTAINER_VOLUME_METRIC_PREFIX)
            || metricName.startsWith(KUBELET_VOLUME_METRIC_PREFIX);
    String rawLabel =
        isVolumeMetric
            ? getAndRemoveLabelValue(metricInfo, PVC)
            : getAndRemoveLabelValue(metricInfo, POD_NAME);
    if (rawLabel == null) {
      return null;
    }

    // Recover the pod name (and, for volume metrics, the datadir suffix). The datadir isn't part
    // of the YBA node name but we append it after resolution so per-volume series stay
    // distinguishable on the graph.
    String podName = rawLabel;
    String datadirSuffix = null;
    if (isVolumeMetric) {
      Matcher m = PVC_DATADIR_POD_PATTERN.matcher(rawLabel);
      if (m.matches()) {
        datadirSuffix = m.group(1);
        podName = m.group(2);
      }
    }

    // Preferred path: ask the resolver, which derives the node name from the universe's cluster /
    // helm-release metadata. That metadata is independent of live NodeDetails records, so it
    // correctly labels historical samples for pods that have since been removed - which is why
    // we can't just look up the current `Universe.getNodes()`.
    String resolvedNodeName =
        k8sPodNodeNameResolver == null
            ? null
            : k8sPodNodeNameResolver.resolveNodeName(podName, namespaceLabel, azNameLabel);
    if (resolvedNodeName != null) {
      return datadirSuffix != null ? resolvedNodeName + "_" + datadirSuffix : resolvedNodeName;
    }

    // Fallback (no resolver, or the resolver couldn't match e.g. because the cluster itself
    // was removed): reconstruct a best-effort node name from the Prometheus labels alone. This
    // path cannot add `-readonly` because we don't know the cluster type, but it still matches
    // YBA's node naming for primary clusters. When the resolver isn't available we default to
    // multi-AZ naming to preserve prior behavior for callers without universe context.
    boolean multiAZ = k8sPodNodeNameResolver == null || k8sPodNodeNameResolver.isMultiAZ();
    String azName = multiAZ ? azNameLabel : null;
    return getInstanceNameLabel(rawLabel, azName, isVolumeMetric);
  }

  private String getInstanceNameLabel(String baseLabel, String azName, boolean extractDatadir) {
    try {
      // The pod name is of the form `<helm-release>-yb-{master,tserver}-<idx>`; strip the release
      // prefix so the label matches YBA's node name (`yb-tserver-0`, `yb-master-2`, etc.). This is
      // applied regardless of `azName` so single-AZ pods aren't rendered as their full pod name.
      String suffix = baseLabel.substring(baseLabel.lastIndexOf("yb-"));
      if (azName != null) {
        suffix += "_" + azName;
      }
      if (extractDatadir) {
        Matcher matcher = Pattern.compile("(datadir\\d+)").matcher(baseLabel);
        if (matcher.find()) {
          suffix += "_" + matcher.group(1);
        }
      }
      return suffix;
    } catch (Exception e) {
      LOG.warn(
          "Error extracting instanceName label: baseLabel={}, azName={}", baseLabel, azName, e);
      return baseLabel;
    }
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
