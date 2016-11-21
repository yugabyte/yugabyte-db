// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.node.ArrayNode;

public class MetricQueryResponse {
  public static class MetricsData {
    public String resultType;
    public ArrayNode result;
  }

  public String status;
  public MetricsData data;
  public String errorType;
  public String error;
}
