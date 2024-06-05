// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

/** This class will be used by the API to generate entries on Slow Queries tab */
public class SlowQueriesParams {

  public String query;

  public int calls;

  public Double total_time;

  public Double min_time;

  public Double max_time;

  public Double mean_time;

  public Double stddev_time;

  public int rows;
}
