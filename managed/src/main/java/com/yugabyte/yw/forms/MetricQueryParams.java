// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;


public class MetricQueryParams {
  @Constraints.Required()
  public String metricKey;

  @Constraints.Required()
  public Long start;

  public Long end;
}
