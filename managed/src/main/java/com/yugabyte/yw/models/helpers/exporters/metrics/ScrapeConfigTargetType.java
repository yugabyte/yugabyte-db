package com.yugabyte.yw.models.helpers.exporters.metrics;

/** Enum for target types that support metrics export scrape configuration. */
public enum ScrapeConfigTargetType {
  MASTER_EXPORT,
  TSERVER_EXPORT,
  YSQL_EXPORT,
  CQL_EXPORT,
  NODE_EXPORT,
  NODE_AGENT_EXPORT,
  OTEL_EXPORT
}
