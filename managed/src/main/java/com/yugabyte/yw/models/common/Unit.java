/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.common;

public enum Unit {
  STATUS(Measure.STATUS, "", "", 0, 1, true),
  COUNT(Measure.COUNT, "", "", 0, Double.MAX_VALUE, true),
  PERCENT(Measure.PERCENTAGE, "%", "pct", 0, 100, false),
  MILLISECOND(Measure.TIME, "ms", "ms", 0, Double.MAX_VALUE, true);

  private final Measure measure;
  private final String displayName;
  private final String metricName;
  private final double minValue;
  private final double maxValue;
  private final boolean integer;

  Unit(
      Measure measure,
      String displayName,
      String metricName,
      double minValue,
      double maxValue,
      boolean integer) {
    this.measure = measure;
    this.displayName = displayName;
    this.metricName = metricName;
    this.minValue = minValue;
    this.maxValue = maxValue;
    this.integer = integer;
  }

  public Measure getMeasure() {
    return measure;
  }

  public String getDisplayName() {
    return displayName;
  }

  public String getMetricName() {
    return metricName;
  }

  public double getMinValue() {
    return minValue;
  }

  public double getMaxValue() {
    return maxValue;
  }

  public boolean isInteger() {
    return integer;
  }
}
