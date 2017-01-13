package org.yb.loadtester.metrics;

import org.apache.log4j.Logger;

public class Metric {
  private static final Logger LOG = Logger.getLogger(Metric.class);
  String name;
  private final Object lock = new Object();
  private long curOpCount = 0;
  private long curOpLatencyMillis = 0;
  private long totalOpCount = 0;

  public Metric(String name) {
    this.name = name;
  }

  public void accumulate(long numOps, long totalLatencyMillis) {
    synchronized(lock) {
      curOpCount += numOps;
      curOpLatencyMillis += totalLatencyMillis;
      totalOpCount++;
    }
  }

  public String getMetricsAndReset() {
    String msg = null;
    synchronized(lock) {
      LOG.debug("currentOpLatency: " + curOpLatencyMillis + ", currentOpCount: " + curOpCount);
      double ops_per_sec =
          (curOpLatencyMillis == 0) ? 0 : (curOpCount * 1000 * 1.0 / curOpLatencyMillis);
      double latency = (curOpCount == 0)?0:(curOpLatencyMillis * 1.0 / curOpCount);
      msg = String.format("%s: %.2f ops/sec (%.2f ms/op), %d total ops",
                          name, ops_per_sec, latency, totalOpCount);
      curOpCount = 0;
      curOpLatencyMillis = 0;
    }
    return msg;
  }
}
