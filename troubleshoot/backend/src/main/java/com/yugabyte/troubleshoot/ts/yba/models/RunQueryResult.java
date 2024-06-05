// Copyright (c) YugaByte, Inc.

package com.yugabyte.troubleshoot.ts.yba.models;

import com.fasterxml.jackson.databind.JsonNode;
import java.util.List;
import lombok.Data;

@Data
public class RunQueryResult {
  private String type;
  private List<JsonNode> result;
}
