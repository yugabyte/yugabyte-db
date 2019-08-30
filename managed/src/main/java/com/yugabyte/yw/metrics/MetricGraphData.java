// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.node.ArrayNode;
import play.libs.Json;
import java.util.Map;

public class MetricGraphData {
  public String name;
  public String type;
  public ArrayNode x = Json.newArray();
  public ArrayNode y = Json.newArray();
  public Map<String, String> labels;
}
