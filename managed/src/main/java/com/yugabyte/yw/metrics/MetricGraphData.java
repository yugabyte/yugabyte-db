// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.node.ArrayNode;
import java.util.Map;
import play.libs.Json;

public class MetricGraphData {
  public String name;
  public String instanceName;
  public String tableName;
  public String tableId;
  public String namespaceName;
  public String type;
  public ArrayNode x = Json.newArray();
  public ArrayNode y = Json.newArray();
  public Map<String, String> labels;
}
