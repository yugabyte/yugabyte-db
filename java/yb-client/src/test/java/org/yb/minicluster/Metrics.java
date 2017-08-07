// Copyright (c) YugaByte, Inc.

package org.yb.minicluster;

import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.util.Map;
import java.util.HashMap;
import java.util.Scanner;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

/**
 * A class to retrieve metrics from a YB server.
 */
public class Metrics {

  /**
   * The base metric.
   */
  public static class Metric {
    public final String name;

    /**
     * Constructs a base {@code Metric}.
     *
     * @param metric  the JSON object that contains the metric
     */
    protected Metric(JsonObject metric) {
      name = metric.get("name").getAsString();
    }
  }

  /**
   * A counter metric.
   */
  public static class Counter extends Metric {
    public final int value;

    /**
     * Constructs a {@code Counter} metric.
     *
     * @param metric  the JSON object that contains the metric
     */
    Counter(JsonObject metric) {
      super(metric);
      value = metric.get("value").getAsInt();
    }
  }

  /**
   * A histogram metric.
   */
  public static class Histogram extends Metric {
    public final int totalCount;
    public final int min;
    public final int mean;
    public final int percentile75;
    public final int percentile95;
    public final int percentile99;
    public final int percentile999;
    public final int percentile9999;
    public final int max;
    public final int totalSum;

    /**
     * Constructs a {@code Histogram} metric.
     *
     * @param metric  the JSON object that contains the metric
     */
    Histogram(JsonObject metric) {
      super(metric);
      totalCount = metric.get("total_count").getAsInt();
      min = metric.get("min").getAsInt();
      mean = metric.get("mean").getAsInt();
      percentile75 = metric.get("percentile_75").getAsInt();
      percentile95 = metric.get("percentile_95").getAsInt();
      percentile99 = metric.get("percentile_99").getAsInt();
      percentile999 = metric.get("percentile_99_9").getAsInt();
      percentile9999 = metric.get("percentile_99_99").getAsInt();
      max = metric.get("max").getAsInt();
      totalSum = metric.get("total_sum").getAsInt();
    }
  }

  // The metrics map.
  Map<String, Metric> map;

  /**
   * Constructs a {@code Metrics} to retrieve the metrics.
   *
   * @param host  the host where the metrics web server is listening
   * @param port  the port where the metrics web server is listening
   * @param type  the metrics type
   */
  public Metrics(String host, int port, String type) throws IOException {
    try {
      map = new HashMap<>();
      URL url = new URL(String.format("http://%s:%d/metrics", host, port));
      Scanner scanner = new Scanner(url.openConnection().getInputStream());
      JsonParser parser = new JsonParser();
      JsonElement tree = parser.parse(scanner.useDelimiter("\\A").next());
      for (JsonElement elem : tree.getAsJsonArray()) {
        JsonObject obj = elem.getAsJsonObject();
        if (obj.get("type").getAsString().equals(type)) {
          for (JsonElement subelem : obj.getAsJsonArray("metrics")) {
            JsonObject metric = subelem.getAsJsonObject();
            if (metric.has("value")) {
              Counter counter = new Counter(metric);
              map.put(counter.name, counter);
            } else if (metric.has("total_count")) {
              Histogram histogram = new Histogram(metric);
              map.put(histogram.name, histogram);
            }
          }
          break;
        }
      }
    } catch (MalformedURLException e) {
      throw new InternalError(e.getMessage());
    }
  }

  /**
   * Retrieves a {@code Counter} metric.
   *
   * @param name  the metric name
   */
  public Counter getCounter(String name) {
    return (Counter)map.get(name);
  }

  /**
   * Retrieves a {@code Histogram} metric.
   *
   * @param name  the metric name
   */
  public Histogram getHistogram(String name) {
    return (Histogram)map.get(name);
  }
}
