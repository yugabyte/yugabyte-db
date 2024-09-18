// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.metrics.*;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.*;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.experimental.Accessors;
import org.apache.commons.collections4.CollectionUtils;
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

  @ApiModelProperty(value = "Metric exclude filters", accessMode = READ_ONLY)
  @JsonProperty("exclude_filters")
  private Map<String, String> excludeFilters = new HashMap<>();

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
    if (context.isSecondLevelAggregation()) {
      String queryStr =
          settings.getAggregatedValueFunction().getAggregationFunction() + "(" + query + ")";
      if (groupBy != null) {
        Set<String> groupBySet = new HashSet<>();
        Set<String> additionalGroupBySet = MetricQueryExecutor.getAdditionalGroupBy(settings);
        groupBySet.addAll(
            Arrays.stream(groupBy.split(","))
                // Drop labels used in the current metric split as want to aggregate across these.
                .filter(
                    (label) ->
                        StringUtils.isNotBlank(label) && !additionalGroupBySet.contains(label))
                .collect(Collectors.toSet()));
        if (!groupBySet.isEmpty()) {
          queryStr = String.format("%s by (%s)", queryStr, String.join(", ", groupBySet));
        }
      }
      return queryStr;
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
    // Special case searches for .avg to convert into the respective ratio of
    // avg(irate(metric_sum)) / avg(irate(metric_count))
    if (metric.endsWith(".avg")) {
      String metricPrefix = metric.substring(0, metric.length() - 4);
      String sumQuery = getQuery(MetricSettings.defaultSettings(metricPrefix + "_sum"), context);
      String countQuery =
          getQuery(MetricSettings.defaultSettings(metricPrefix + "_count"), context);
      return context.isSecondLevelAggregation()
          // Filter out the `NaN` values from divide by 0 when performing second level aggregation.
          ? String.format("(%s) / (%s != 0)", sumQuery, countQuery, countQuery)
          : String.format("(%s) / (%s)", sumQuery, countQuery);
    } else if (metric.contains("/")) {
      String[] metricNames = metric.split("/");
      MetricConfigDefinition numerator = MetricConfig.get(metricNames[0]).getConfig();
      MetricConfigDefinition denominator = MetricConfig.get(metricNames[1]).getConfig();
      String numQuery =
          numerator.getQuery(MetricSettings.defaultSettings(numerator.metric), context);
      String denomQuery =
          denominator.getQuery(MetricSettings.defaultSettings(denominator.metric), context);
      return context.isSecondLevelAggregation()
          // Filter out the `NaN` values from divide by 0 when performing second level aggregation.
          ? String.format("((%s) / (%s != 0)) * 100", numQuery, denomQuery, denomQuery)
          : String.format("((%s) / (%s)) * 100", numQuery, denomQuery);
    }

    String queryStr;
    StringBuilder query = new StringBuilder();
    query.append(metric);

    // If we have additional filters, we add them
    Map<String, String> allFilters = new HashMap<>(filters);
    Map<String, String> allExcludeFilters = new HashMap<>(excludeFilters);
    if (!context.getAdditionalFilters().isEmpty()) {
      allFilters.putAll(context.getAdditionalFilters());
      // The kubelet volume metrics only has the persistentvolumeclain field
      // as well as namespace. Adding any other field will cause the query to fail.
      if (metric.startsWith("kubelet_volume")) {
        allFilters.remove("pod_name");
        allFilters.remove("container_name");
      }
      // For all other metrics, it is safe to remove the filter if
      // it exists.
      else {
        allFilters.remove("persistentvolumeclaim");
      }
    }
    allExcludeFilters.putAll(context.getExcludeFilters());
    if (!allFilters.isEmpty() || !allExcludeFilters.isEmpty()) {
      query.append(filtersToString(allFilters, allExcludeFilters));
    }

    // Range is applicable only when we have functions
    // TODO: also need to add a check, since range is applicable for only certain functions
    if (range != null && function != null) {
      query.append(String.format("[%ds]", context.getQueryRangeSecs())); // for ex: [60s]
    }
    if (context.getQueryTimestampSec() != null) {
      query.append(String.format("@%d", context.getQueryTimestampSec())); // for ex: @1609746000
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
      for (String functionName : functions) {
        if (functionName.startsWith("quantile_over_time")) {
          String percentile = functionName.split("[.]")[1];
          queryStr = String.format("quantile_over_time(0.%s, %s)", percentile, queryStr);
        } else if (functionName.equals("subtract_metric")) {
          queryStr = String.format("%s - %s", queryStr, metric);
        } else if (functionName.startsWith("topk") || functionName.startsWith("bottomk")) {
          if (settings.getSplitMode() != SplitMode.NONE) {
            // TopK/BottomK is requested explicitly. Just ignore default split.
            continue;
          }
          String fun = functionName.split("[.]")[0];
          String count = functionName.split("[.]")[1];
          queryStr = String.format(fun + "(%s, %s)", count, queryStr);
        } else {
          queryStr = String.format("%s(%s)", functionName, queryStr);
        }
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
  private String filtersToString(Map<String, String> filters, Map<String, String> excludeFilters) {
    List<String> filtersList = new ArrayList<>();
    for (Map.Entry<String, String> filter : filters.entrySet()) {
      if (specialFilterPattern.matcher(filter.getValue()).find()) {
        filtersList.add(filter.getKey() + "=~\"" + filter.getValue() + "\"");
      } else {
        filtersList.add(filter.getKey() + "=\"" + filter.getValue() + "\"");
      }
    }
    for (Map.Entry<String, String> excludeFilter : excludeFilters.entrySet()) {
      if (specialFilterPattern.matcher(excludeFilter.getValue()).find()) {
        filtersList.add(excludeFilter.getKey() + "!~\"" + excludeFilter.getValue() + "\"");
      } else {
        filtersList.add(excludeFilter.getKey() + "!=\"" + excludeFilter.getValue() + "\"");
      }
    }
    return "{" + String.join(", ", filtersList) + "}";
  }
}
