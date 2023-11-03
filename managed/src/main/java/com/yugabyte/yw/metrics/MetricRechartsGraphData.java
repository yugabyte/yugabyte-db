// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.fasterxml.jackson.databind.node.ArrayNode;
import java.util.ArrayList;
import java.util.Map;
import play.libs.Json;

public class MetricRechartsGraphData {
  public ArrayList<String> names = new ArrayList<String>();
  public String type;
  public ArrayNode data = Json.newArray();
  public ArrayList<Map<String, String>> labels = new ArrayList<Map<String, String>>();
}
