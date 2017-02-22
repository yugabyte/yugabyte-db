package org.yb.loadtester.metrics;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.apache.log4j.Logger;

public class MetricsTracker extends Thread {
  private static final Logger LOG = Logger.getLogger(MetricsTracker.class);

  // Interface to print custom messages.
  public static interface StatusMessageAppender {
    public String appenderName();
    public void appendMessage(StringBuilder sb);
  }

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
  // Map of custom appenders.
  Map<String, StatusMessageAppender> appenders =
      new ConcurrentHashMap<String, StatusMessageAppender>();

  public MetricsTracker() {
    this.setDaemon(true);
  }

  public void registerStatusMessageAppender(StatusMessageAppender appender) {
    appenders.put(appender.appenderName(), appender);
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

  public void getMetricsAndReset(StringBuilder sb) {
    for (MetricName metricName : MetricName.values()) {
      sb.append(String.format("%s  |  ", metrics.get(metricName).getMetricsAndReset()));
    }
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
        StringBuilder sb = new StringBuilder();
        getMetricsAndReset(sb);
        for (StatusMessageAppender appender : appenders.values()) {
          appender.appendMessage(sb);
        }
        LOG.info(sb.toString());
      } catch (InterruptedException e) {}
    }
  }
}
