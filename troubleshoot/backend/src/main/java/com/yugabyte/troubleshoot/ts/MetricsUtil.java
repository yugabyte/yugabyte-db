package com.yugabyte.troubleshoot.ts;

import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import io.prometheus.client.Summary;

public class MetricsUtil {

  public static String LABEL_RESULT = "result";
  public static String RESULT_SUCCESS = "success";
  public static String RESULT_FAILURE = "failure";

  public static Summary buildSummary(String name, String description, String... labelNames) {
    return Summary.build(name, description)
        .quantile(0.5, 0.05)
        .quantile(0.9, 0.01)
        .labelNames(labelNames)
        .register(CollectorRegistry.defaultRegistry);
  }

  public static Counter buildCounter(String name, String description, String... labelNames) {
    return Counter.build(name, description)
        .labelNames(labelNames)
        .register(CollectorRegistry.defaultRegistry);
  }
}
