// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.metrics.MetricAggregation;
import com.yugabyte.yw.metrics.MetricSettings;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Transient;
import play.libs.Json;

@ApiModel(description = "Metric configuration key and value for Prometheus")
@Entity
public class MetricConfig extends Model {

  public static final String METRICS_CONFIG_PATH = "metric/metrics.yml";

  public static class Layout {
    public static class Axis {
      public String type;
      public Map<String, String> alias = new HashMap<>();
      public String ticksuffix;
      public String tickformat;
    }

    public String title;
    public Axis xaxis;
    public Axis yaxis;
  }

  @Transient public String metric;
  @Transient public String function;
  @Transient public String range;
  @Transient public Map<String, String> filters = new HashMap<>();
  @Transient public String group_by;
  @Transient public String panel_group;
  @Transient public Layout layout = new Layout();
  @Transient public String operator;

  @ApiModelProperty(value = "Metrics configuration key", accessMode = READ_ONLY)
  @Id
  @Column(length = 100)
  private String config_key;

  public String getKey() {
    return config_key;
  }

  public void setKey(String key) {
    this.config_key = key;
  }

  // TODO : update this field once json cleanup is done.
  @ApiModelProperty(value = "Metrics configuration value", accessMode = READ_WRITE)
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  private JsonNode config;

  public void setConfig(JsonNode config) {
    this.config = config;
  }

  public MetricConfig getConfig() {
    return Json.fromJson(this.config, MetricConfig.class);
  }

  // If we have any special filter pattern, then we need to use =~ instead
  // of = in our filter condition. Special patterns include *, |, $ or +.
  static final Pattern specialFilterPattern = Pattern.compile("[*|+$]");

  public Map<String, String> getFilters() {
    return this.getConfig().filters;
  }

  public Layout getLayout() {
    return this.getConfig().layout;
  }

  public Map<String, String> getQueries(Map<String, String> additionalFilters, int queryRangeSecs) {
    MetricSettings metricSettings = MetricSettings.defaultSettings(getConfig().metric);
    return getQueries(metricSettings, additionalFilters, queryRangeSecs);
  }

  public Map<String, String> getQueries(
      MetricSettings metricSettings, Map<String, String> additionalFilters, int queryRangeSecs) {
    MetricConfig metricConfig = getConfig();
    if (metricConfig.metric == null) {
      throw new RuntimeException("Invalid MetricConfig: metric attribute is required");
    }
    Map<String, String> output = new HashMap<>();
    // We allow for the metric to be a | separated list of metric names, case in which we will split
    // and execute the metric queries individually.
    // Note: contains takes actual chars, while split takes a regex, hence the escape \\ there.
    if (metricConfig.metric.contains("|")) {
      for (String m : metricConfig.metric.split("\\|")) {
        output.put(m, getQuery(metricSettings.cloneWithName(m), additionalFilters, queryRangeSecs));
      }
    } else {
      output.put(
          metricConfig.metric,
          getQuery(
              metricSettings.cloneWithName(metricConfig.metric),
              additionalFilters,
              queryRangeSecs));
    }
    return output;
  }

  public String getQuery(Map<String, String> additionalFilters, int queryRangeSecs) {
    MetricSettings metricSettings = MetricSettings.defaultSettings(this.getConfig().metric);
    return getQuery(metricSettings, additionalFilters, queryRangeSecs);
  }

  /**
   * This method construct the prometheus queryString based on the metric config if additional
   * filters are provided, it applies those filters as well. example query string: -
   * avg(collectd_cpu_percent{cpu="system"}) - rate(collectd_cpu_percent{cpu="system"}[30m]) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) /10
   *
   * @return, a valid prometheus query string
   */
  public String getQuery(
      MetricSettings metricSettings, Map<String, String> additionalFilters, int queryRangeSecs) {
    // Special case searchs for .avg to convert into the respective ratio of
    // avg(irate(metric_sum)) / avg(irate(metric_count))
    String metric = metricSettings.getMetric();
    if (metric.endsWith(".avg")) {
      String metricPrefix = metric.substring(0, metric.length() - 4);
      String sumQuery =
          getQuery(
              MetricSettings.defaultSettings(metricPrefix + "_sum"),
              additionalFilters,
              queryRangeSecs);
      String countQuery =
          getQuery(
              MetricSettings.defaultSettings(metricPrefix + "_count"),
              additionalFilters,
              queryRangeSecs);
      return "(" + sumQuery + ") / (" + countQuery + ")";
    } else if (metric.contains("/")) {
      String[] metricNames = metric.split("/");
      MetricConfig numerator = get(metricNames[0]);
      MetricConfig denominator = get(metricNames[1]);
      String numQuery =
          numerator.getQuery(
              MetricSettings.defaultSettings(numerator.getConfig().metric),
              additionalFilters,
              queryRangeSecs);
      String denomQuery =
          denominator.getQuery(
              MetricSettings.defaultSettings(denominator.getConfig().metric),
              additionalFilters,
              queryRangeSecs);
      return String.format("((%s)/(%s))*100", numQuery, denomQuery);
    }

    String queryStr;
    StringBuilder query = new StringBuilder();
    MetricConfig metricConfig = getConfig();
    query.append(metric);

    Map<String, String> filters = this.getFilters();
    // If we have additional filters, we add them
    if (!additionalFilters.isEmpty()) {
      filters.putAll(additionalFilters);
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
    if (metricConfig.range != null && metricConfig.function != null) {
      query.append(String.format("[%ds]", queryRangeSecs)); // for ex: [60s]
    }

    queryStr = query.toString();

    if (metricConfig.function != null) {
      String[] functions = metricConfig.function.split("\\|");
      /* We have added special way to represent multiple functions that we want to
      do, we pipe delimit those, but they follow an order.
      Scenario 1:
        function: rate|avg,
        query str: avg(rate(metric{memory="used"}[30m]))
      Scenario 2:
        function: rate
        query str: rate(metric{memory="used"}[30m]). */
      if (metricSettings.getAggregation() != MetricAggregation.DEFAULT) {
        if (MetricAggregation.TIME_AGGREGATIONS.contains(functions[0])) {
          functions[0] = metricSettings.getAggregation().getTimeAggregationFunc();
        }
        if (MetricAggregation.NODE_AGGREGATIONS.contains(functions[functions.length - 1])) {
          functions[functions.length - 1] =
              metricSettings.getAggregation().getNodeAggregationFunc();
        }
      }
      if (functions.length > 1) {
        // We need to split the multiple functions and form the query string
        for (String functionName : functions) {
          queryStr = String.format("%s(%s)", functionName, queryStr);
        }
      } else {
        queryStr = String.format("%s(%s)", metricConfig.function, queryStr);
      }
    }

    if (getConfig().group_by != null) {
      queryStr = String.format("%s by (%s)", queryStr, metricConfig.group_by);
    }
    if (getConfig().operator != null) {
      queryStr = String.format("%s %s", queryStr, metricConfig.operator);
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

  public static final Finder<String, MetricConfig> find =
      new Finder<String, MetricConfig>(MetricConfig.class) {};

  /**
   * returns metric config for the given key
   *
   * @param configKey
   * @return MetricConfig
   */
  public static MetricConfig get(String configKey) {
    return MetricConfig.find.byId(configKey);
  }

  /**
   * Create a new instance of metric config for given config key and config data
   *
   * @param configKey
   * @param configData
   * @return returns a instance of MetricConfig
   */
  public static MetricConfig create(String configKey, JsonNode configData) {
    MetricConfig metricConfig = new MetricConfig();
    metricConfig.setKey(configKey);
    metricConfig.setConfig(Json.toJson(configData));
    return metricConfig;
  }

  /**
   * Loads the configs into the db, if the config already exists it would update that if not it will
   * create new config.
   *
   * @param configs
   */
  public static void loadConfig(Map<String, Object> configs) {
    List<String> currentConfigs =
        MetricConfig.find.all().stream().map(MetricConfig::getKey).collect(Collectors.toList());

    for (Map.Entry<String, Object> configData : configs.entrySet()) {
      MetricConfig metricConfig =
          MetricConfig.create(configData.getKey(), Json.toJson(configData.getValue()));
      // Check if the config already exists if so, let's update it or else,
      // we will create new one.
      if (currentConfigs.contains(metricConfig.getKey())) {
        metricConfig.update();
      } else {
        metricConfig.save();
      }
    }
  }
}
