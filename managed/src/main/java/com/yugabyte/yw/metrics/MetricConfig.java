// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import java.util.HashMap;
import java.util.Map;

public class MetricConfig {
  public class Layout {
    public class Axis {
      public String type;
      public Map<String, String> alias = new HashMap<>();
    }

    public String title;
    public Axis xaxis;
    public Axis yaxis;
  }

  public String metric;
  public String function;
  public String range;
  public Map<String, String> filters = new HashMap<>();
  public String group_by;
  public Layout layout;


  /**
   * We construct the prometheus queryString based on the metric definition we have in metrics.yml
   * for the given metric type.
   * example query string:
   *  - avg(collectd_cpu_percent{cpu="system"})
   *  - rate(collectd_cpu_percent{cpu="system"}[30m])
   *  - avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory)
   *
   * @return, a valid prometheus query string
   */
  public String getQuery(Map<String, String> additionalFilters) {
    String queryStr;
    StringBuilder query = new StringBuilder();
    if (metric == null) {
      throw new RuntimeException("Invalid MetricConfig: metric attribute is required");
    }
    query.append(metric);

    if (!filters.isEmpty()) {
      filters.putAll(additionalFilters);
      query.append(filtersString(filters));
    }

    // Range is applicable only we have functions
    // TODO: also need to add a check, since range is applicable for only certain functions
    if (range != null && function != null) {
      query.append(String.format("[%s]", range));
    }

    queryStr = query.toString();

    if (function != null) {
      if (function.contains("|")) {
        // We need to split the multiple functions and form the query string
        String[] functions = function.split("\\|");
        for (String functionName : functions) {
          queryStr = String.format("%s(%s)", functionName, queryStr);
        }
      } else {
        queryStr = String.format("%s(%s)", function, queryStr);
      }
    }

    if (group_by != null) {
      queryStr = String.format("%s by (%s)", queryStr, group_by);
    }

    return queryStr;
  }

  /**
   * MapToString method converts a map to a string with quotes around the value
   * the reason we have to do this way is because prometheus expects the json
   * key to have no quote, and just value should have double quotes.
   * @param filters is map<String, String>
   * @return String representation of the map
   */
  private String filtersString(Map<String, String> filters) {
    StringBuilder filterStr = new StringBuilder();
    String prefix = "{";
    for(Map.Entry<String, String> filter : filters.entrySet()) {
      filterStr.append(prefix);
      // If we have the pipe delimiter in the filter, that means we want to match
      // multiple filter conditions
      if (filter.getValue().contains("|")) {
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
