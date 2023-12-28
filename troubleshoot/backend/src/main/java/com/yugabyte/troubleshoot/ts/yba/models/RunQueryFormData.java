// Copyright (c) YugaByte, Inc.

package com.yugabyte.troubleshoot.ts.yba.models;

import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import lombok.Data;

@Data
@JsonNaming(PropertyNamingStrategies.SnakeCaseStrategy.class)
public class RunQueryFormData {

  private String query;

  private String dbName;

  private String nodeName;
}
