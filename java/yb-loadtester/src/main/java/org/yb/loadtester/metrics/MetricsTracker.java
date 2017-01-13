package org.yb.loadtester.metrics;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.apache.log4j.Logger;

public class MetricsTracker extends Thread {
  private static final Logger LOG = Logger.getLogger(MetricsTracker.class);
  // The type of metrics supported.
  public static enum MetricName {
    Read,
    Write,
  }
  // Map to store all the metrics objects.
  Map<MetricName, Metric> metrics = new ConcurrentHashMap<MetricName, Metric>();
  // Track reads.
  Metric reads = new Metric("Reads");
  // Track writes.
  Metric writes = new Metric("Writes");
  // State variable to make sure this thread is started exactly once.
  boolean hasStarted = false;
  Object initLock = new Object();

  public MetricsTracker() {
    this.setDaemon(true);
  }

  public void createMetric(MetricName metricName) {
    synchronized (initLock) {
      if (!metrics.containsKey(metricName)) {
        metrics.put(metricName, new Metric(metricName.name()));
      }
    }
  }

  public Metric getMetric(MetricName metricName) {
    return metrics.get(metricName);
  }

  public String getMetricsAndReset() {
    StringBuilder sb = new StringBuilder();
    for (MetricName metricName : MetricName.values()) {
      sb.append(String.format("%s  |  ", metrics.get(metricName).getMetricsAndReset()));
    }
    return sb.toString();
  }

  @Override
  public void start() {
    synchronized (initLock) {
      if (!hasStarted) {
        hasStarted = true;
        super.start();
      }
    }
  }

  @Override
  public void run() {
    while (true) {
      try {
        Thread.sleep(5000);
        LOG.info(getMetricsAndReset());
      } catch (InterruptedException e) {}
    }
  }
}
