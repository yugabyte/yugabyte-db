package com.yugabyte.troubleshoot.ts.models;

import lombok.Getter;

@Getter
public enum RuntimeConfigKey {
  QUERY_LATENCY_BATCH_SIZE("anomaly.query_latency.batch_size"),
  QUERY_LATENCY_MIN_ANOMALY_DURATION("anomaly.query_latency.min_anomaly_duration"),
  QUERY_LATENCY_MIN_ANOMALY_VALUE("anomaly.query_latency.min_anomaly_value"),
  QUERY_LATENCY_BASELINE_POINTS_RATIO("anomaly.query_latency.baseline_points_ratio"),
  QUERY_LATENCY_THRESHOLD_RATIO("anomaly.query_latency.threshold_ratio"),

  SLOW_DISKS_MIN_ANOMALY_DURATION("anomaly.slow_disks.min_anomaly_duration"),
  SLOW_DISKS_THRESHOLD_IO_TIME("anomaly.slow_disks.threshold.disk_io_time"),
  SLOW_DISKS_THRESHOLD_QUEUE_SIZE("anomaly.slow_disks.threshold.disk_io_queue_depth"),

  UNEVEN_CPU_MIN_ANOMALY_DURATION("anomaly.uneven_cpu.min_anomaly_duration"),
  UNEVEN_CPU_MIN_ANOMALY_VALUE("anomaly.uneven_cpu.min_anomaly_value"),
  UNEVEN_CPU_THRESHOLD("anomaly.uneven_cpu.threshold_ratio"),

  UNEVEN_QUERY_MIN_ANOMALY_DURATION("anomaly.uneven_query.min_anomaly_duration"),
  UNEVEN_QUERY_MIN_ANOMALY_VALUE("anomaly.uneven_query.min_anomaly_value"),
  UNEVEN_QUERY_THRESHOLD("anomaly.uneven_query.threshold_ratio"),

  UNEVEN_YSQL_QUERY_MIN_ANOMALY_DURATION("anomaly.uneven_ysql_query.min_anomaly_duration"),
  UNEVEN_YSQL_QUERY_MIN_ANOMALY_VALUE("anomaly.uneven_ysql_query.min_anomaly_value"),
  UNEVEN_YSQL_QUERY_THRESHOLD("anomaly.uneven_ysql_query.threshold_ratio"),

  UNEVEN_DATA_MIN_ANOMALY_DURATION("anomaly.uneven_data.min_anomaly_duration"),
  UNEVEN_DATA_MIN_ANOMALY_VALUE("anomaly.uneven_data.min_anomaly_value"),
  UNEVEN_DATA_THRESHOLD("anomaly.uneven_data.threshold_ratio");

  private final String path;

  RuntimeConfigKey(String path) {
    this.path = path;
  }
}
