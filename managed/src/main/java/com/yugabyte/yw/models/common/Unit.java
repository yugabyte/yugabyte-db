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
  STATUS(Measure.STATUS, "", ""),
  COUNT(Measure.COUNT, "", ""),
  PERCENT(Measure.PERCENTAGE, "%", "pct"),
  MILLISECOND(Measure.TIME, "ms", "ms");

  private final Measure measure;
  private final String displayName;
  private final String metricName;

  Unit(Measure measure, String displayName, String metricName) {
    this.measure = measure;
    this.displayName = displayName;
    this.metricName = metricName;
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
}
