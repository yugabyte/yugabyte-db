// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.List;


public class MetricQueryParams {
  @Constraints.Required()
  public List<String> metrics;

  @Constraints.Required()
  public Long start;

  public Long end;

  public String nodePrefix;

  public String nodeName;
}
