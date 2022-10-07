// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.metrics.MetricLabelFilters;
import com.yugabyte.yw.metrics.MetricQueryContext;
import com.yugabyte.yw.metrics.MetricSettings;
import com.yugabyte.yw.metrics.NodeAggregation;
import com.yugabyte.yw.metrics.SplitMode;
import com.yugabyte.yw.metrics.TimeAggregation;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.experimental.Accessors;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@ApiModel(description = "Metric configuration definition")
@Data
@Accessors(chain = true)
public class MetricConfigDefinition {

  @ApiModel(description = "Metric layout")
  @Data
  @Accessors(chain = true)
  public static class Layout {
    @ApiModel(description = "Metric axis layout")
    @Data
    @Accessors(chain = true)
    public static class Axis {
      @ApiModelProperty(value = "Axis unit", accessMode = READ_ONLY)
      private String type;

      @ApiModelProperty(value = "Aliases for metric names", accessMode = READ_ONLY)
      private Map<String, String> alias = new HashMap<>();

      @ApiModelProperty(value = "Axis unit", accessMode = READ_ONLY)
      private String ticksuffix;

      @ApiModelProperty(value = "Axis value display format", accessMode = READ_ONLY)
      private String tickformat;
    }

    @ApiModelProperty(value = "Graph title", accessMode = READ_ONLY)
    private String title;

    @ApiModelProperty(value = "X Axis layout", accessMode = READ_ONLY)
    private Axis xaxis;

    @ApiModelProperty(value = "Y Axis layour", accessMode = READ_ONLY)
    private Axis yaxis;
  }

  @ApiModelProperty(value = "Metrics name", accessMode = READ_ONLY)
  private String metric;

  @ApiModelProperty(value = "Functions, used to generate metric expression", accessMode = READ_ONLY)
  private String function;

  @ApiModelProperty(
      value = "Flag, indicating that metric is using time aggregation",
      accessMode = READ_ONLY)
  private String range;

  @ApiModelProperty(value = "Metric filters", accessMode = READ_ONLY)
  private Map<String, String> filters = new HashMap<>();

  @ApiModelProperty(value = "Fields, used to group metric query result", accessMode = READ_ONLY)
  @JsonProperty("group_by")
  private String groupBy;

  @ApiModelProperty(value = "Panel group name for Grafana dashboard", accessMode = READ_ONLY)
  @JsonProperty("panel_group")
  private String panelGroup;

  @ApiModelProperty(value = "Layout details", accessMode = READ_ONLY)
  private Layout layout = new Layout();

  @ApiModelProperty(value = "Operator, applied to metric value", accessMode = READ_ONLY)
  private String operator;

  // If we have any special filter pattern, then we need to use =~ instead
  // of = in our filter condition. Special patterns include *, |, $ or +.
  static final Pattern specialFilterPattern = Pattern.compile("[*|+$]");

  public Map<String, String> getQueries(Map<String, String> additionalFilters, int queryRangeSecs) {
    MetricSettings metricSettings = MetricSettings.defaultSettings(metric);
    return getQueries(
        metricSettings,
        MetricQueryContext.builder()
            .queryRangeSecs(queryRangeSecs)
            .additionalFilters(additionalFilters)
            .build());
  }

  public Map<String, String> getQueries(MetricSettings settings, MetricQueryContext context) {
    if (metric == null) {
      throw new RuntimeException("Invalid MetricConfig: metric attribute is required");
    }
    Map<String, String> output = new LinkedHashMap<>();
    // We allow for the metric to be a | separated list of metric names, case in which we will split
    // and execute the metric queries individually.
    // Note: contains takes actual chars, while split takes a regex, hence the escape \\ there.
    if (metric.contains("|")) {
      for (String m : metric.split("\\|")) {
        output.put(m, getSingleMetricQuery(settings.cloneWithName(m), context));
      }
    } else {
      output.put(metric, getSingleMetricQuery(settings.cloneWithName(metric), context));
    }
    return output;
  }

  public String getSingleMetricQuery(MetricSettings settings, MetricQueryContext context) {
    String query = getQuery(settings, context);
    if (context.isTopKQuery()) {
      String topGroupBy = StringUtils.EMPTY;
      if (groupBy != null) {
        Set<String> topGroupByLabels =
            Arrays.stream(groupBy.split(","))
                .filter(StringUtils::isNotBlank)
                .collect(Collectors.toSet());
        // No need to group by additional labels for top K query
        topGroupByLabels.removeAll(context.getAdditionalGroupBy());
        if (CollectionUtils.isNotEmpty(topGroupByLabels)) {
          topGroupBy = " by (" + String.join(", ", topGroupByLabels) + ")";
        }
      }
      switch (settings.getSplitMode()) {
        case TOP:
          return "topk(" + settings.getSplitCount() + ", " + query + ")" + topGroupBy;
        case BOTTOM:
          return "bottomk(" + settings.getSplitCount() + ", " + query + ")" + topGroupBy;
        default:
          throw new IllegalArgumentException(
              "Unexpected split mode "
                  + settings.getSplitMode().name()
                  + " for top/bottom K query");
      }
    }
    return query;
  }

  public String getQuery(Map<String, String> additionalFilters, int queryRangeSecs) {
    MetricSettings metricSettings = MetricSettings.defaultSettings(metric);
    return getQuery(
        metricSettings,
        MetricQueryContext.builder()
            .queryRangeSecs(queryRangeSecs)
            .additionalFilters(additionalFilters)
            .build());
  }

  /**
   * This method construct the prometheus queryString based on the metric config if additional
   * filters are provided, it applies those filters as well. example query string: -
   * avg(collectd_cpu_percent{cpu="system"}) - rate(collectd_cpu_percent{cpu="system"}[30m]) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) /10
   *
   * @return a valid prometheus query string
   */
  public String getQuery(MetricSettings settings, MetricQueryContext context) {
    String metric = settings.getMetric();
    List<MetricLabelFilters> orFiltersList =
        context.getMetricOrFilters().getOrDefault(metric, Collections.emptyList());
    if (CollectionUtils.isEmpty(orFiltersList)) {
      return getOrQuery(settings, context);
    }
    List<String> orQueries = new ArrayList<>();
    for (MetricLabelFilters orFilters : orFiltersList) {
      Map<String, String> newAdditionalFilters = new HashMap<>(context.getAdditionalFilters());
      orFilters
          .getFilters()
          .forEach(orFilter -> newAdditionalFilters.put(orFilter.getLabel(), orFilter.getFilter()));
      orQueries.add(
          getOrQuery(
              settings, context.toBuilder().additionalFilters(newAdditionalFilters).build()));
    }
    if (settings.getSplitMode() != SplitMode.NONE && settings.isReturnAggregatedValue()) {
      // In case of mean query we need to remove exported_instance grouping to get average from
      // the metrics, which initially has this grouping
      orQueries.add(
          getOrQuery(
              settings,
              context
                  .toBuilder()
                  .removeGroupBy(context.getAdditionalGroupBy())
                  .additionalGroupBy(Collections.emptySet())
                  .build()));
    }
    return "(" + String.join(") or (", orQueries) + ")";
  }

  /**
   * This method construct the prometheus queryString based on the metric config if additional
   * filters are provided, it applies those filters as well. example query string: -
   * avg(collectd_cpu_percent{cpu="system"}) - rate(collectd_cpu_percent{cpu="system"}[30m]) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) /10
   *
   * @return a valid prometheus query string
   */
  private String getOrQuery(MetricSettings settings, MetricQueryContext context) {
    String metric = settings.getMetric();
    // Special case searches for .avg to convert into the respective ratio of
    // avg(irate(metric_sum)) / avg(irate(metric_count))
    if (metric.endsWith(".avg")) {
      String metricPrefix = metric.substring(0, metric.length() - 4);
      String sumQuery = getQuery(MetricSettings.defaultSettings(metricPrefix + "_sum"), context);
      String countQuery =
          getQuery(MetricSettings.defaultSettings(metricPrefix + "_count"), context);
      return "(" + sumQuery + ") / (" + countQuery + ")";
    } else if (metric.contains("/")) {
      String[] metricNames = metric.split("/");
      MetricConfigDefinition numerator = MetricConfig.get(metricNames[0]).getConfig();
      MetricConfigDefinition denominator = MetricConfig.get(metricNames[1]).getConfig();
      String numQuery =
          numerator.getQuery(MetricSettings.defaultSettings(numerator.metric), context);
      String denomQuery =
          denominator.getQuery(MetricSettings.defaultSettings(denominator.metric), context);
      return String.format("((%s)/(%s))*100", numQuery, denomQuery);
    }

    String queryStr;
    StringBuilder query = new StringBuilder();
    query.append(metric);

    // If we have additional filters, we add them
    if (!context.getAdditionalFilters().isEmpty()) {
      filters.putAll(context.getAdditionalFilters());
      // The kubelet volume metrics only has the persistentvolumeclain field
      // as well as namespace. Adding any other field will cause the query to fail.
      if (metric.startsWith("kubelet_volume")) {
        filters.remove("pod_name");
        filters.remove("container_name");
      }
      // For all other metrics, it is safe to remove the filter if
      // it exists.
      else {
        filters.remove("persistentvolumeclaim");
      }
    }

    if (!filters.isEmpty()) {
      query.append(filtersToString(filters));
    }

    // Range is applicable only when we have functions
    // TODO: also need to add a check, since range is applicable for only certain functions
    if (range != null && function != null) {
      query.append(String.format("[%ds]", context.getQueryRangeSecs())); // for ex: [60s]
    }

    queryStr = query.toString();

    if (function != null) {
      String[] functions = function.split("\\|");
      /* We have added special way to represent multiple functions that we want to
      do, we pipe delimit those, but they follow an order.
      Scenario 1:
        function: rate|avg,
        query str: avg(rate(metric{memory="used"}[30m]))
      Scenario 2:
        function: rate
        query str: rate(metric{memory="used"}[30m]). */
      if (settings.getTimeAggregation() != TimeAggregation.DEFAULT) {
        if (TimeAggregation.AGGREGATION_FUNCTIONS.contains(functions[0])) {
          functions[0] = settings.getTimeAggregation().getAggregationFunction();
        }
      }
      if (settings.getNodeAggregation() != NodeAggregation.DEFAULT) {
        if (NodeAggregation.AGGREGATION_FUNCTIONS.contains(functions[functions.length - 1])) {
          functions[functions.length - 1] = settings.getNodeAggregation().getAggregationFunction();
        }
      }
      if (functions.length > 1) {
        // We need to split the multiple functions and form the query string
        for (String functionName : functions) {
          queryStr = String.format("%s(%s)", functionName, queryStr);
        }
      } else {
        queryStr = String.format("%s(%s)", function, queryStr);
      }
    }

    if (groupBy != null || CollectionUtils.isNotEmpty(context.getAdditionalGroupBy())) {
      Set<String> groupBySet = new HashSet<>();
      if (groupBy != null) {
        groupBySet.addAll(
            Arrays.stream(groupBy.split(","))
                .filter(StringUtils::isNotBlank)
                .collect(Collectors.toSet()));
      }
      groupBySet.addAll(context.getAdditionalGroupBy());
      groupBySet.removeAll(context.getRemoveGroupBy());
      queryStr = String.format("%s by (%s)", queryStr, String.join(", ", groupBySet));
    }
    if (operator != null) {
      queryStr = String.format("%s %s", queryStr, operator);
    }
    return queryStr;
  }

  /**
   * filtersToString method converts a map to a string with quotes around the value. The reason we
   * have to do this way is because prometheus expects the json key to have no quote, and just value
   * should have double quotes.
   *
   * @param filters is map<String, String>
   * @return String representation of the map ex: {memory="used", extra="1"} {memory="used"}
   *     {type=~"iostat_write_count|iostat_read_count"}
   */
  private String filtersToString(Map<String, String> filters) {
    StringBuilder filterStr = new StringBuilder();
    String prefix = "{";
    for (Map.Entry<String, String> filter : filters.entrySet()) {
      filterStr.append(prefix);
      if (specialFilterPattern.matcher(filter.getValue()).find()) {
        filterStr.append(filter.getKey() + "=~\"" + filter.getValue() + "\"");
      } else {
        filterStr.append(filter.getKey() + "=\"" + filter.getValue() + "\"");
      }
      prefix = ", ";
    }
    filterStr.append("}");
    return filterStr.toString();
  }
}
