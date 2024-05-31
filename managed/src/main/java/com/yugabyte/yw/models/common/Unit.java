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

import static com.yugabyte.yw.models.common.Condition.GREATER_THAN;
import static com.yugabyte.yw.models.common.Condition.LESS_THAN;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.Getter;
import lombok.experimental.Accessors;

@Getter
@ApiModel
public enum Unit {
  STATUS(
      new UnitBuilder()
          .measure(Measure.STATUS)
          .maxValue(1)
          .integer(true)
          .thresholdReadOnly(true)
          .thresholdConditionReadOnly(true)
          .thresholdCondition(LESS_THAN)),
  COUNT(new UnitBuilder().measure(Measure.COUNT).integer(true)),
  PERCENT(
      new UnitBuilder()
          .measure(Measure.PERCENTAGE)
          .displayName("%")
          .metricName("pct")
          .maxValue(100)
          .thresholdConditionReadOnly(true)),
  MILLISECOND(
      new UnitBuilder()
          .measure(Measure.TIME)
          .displayName("ms")
          .metricName("ms")
          .integer(true)
          .thresholdConditionReadOnly(true)),
  SECOND(
      new UnitBuilder()
          .measure(Measure.TIME)
          .displayName("sec")
          .metricName("sec")
          .integer(true)
          .thresholdConditionReadOnly(true)),
  DAY(
      new UnitBuilder()
          .measure(Measure.TIME)
          .displayName("day(s)")
          .metricName("day")
          .integer(true)
          .thresholdConditionReadOnly(true)),
  MEGABYTE(
      new UnitBuilder()
          .measure(Measure.SIZE)
          .displayName("MB(s)")
          .metricName("MB")
          .integer(true)
          .thresholdConditionReadOnly(true));

  private final Measure measure;
  private final String displayName;
  private final String metricName;
  private final double minValue;
  private final double maxValue;
  private final boolean integer;
  private final boolean thresholdReadOnly;
  private final boolean thresholdConditionOnly;
  private final Condition thresholdCondition;

  Unit(UnitBuilder builder) {
    this.measure = builder.measure();
    this.displayName = builder.displayName();
    this.metricName = builder.metricName();
    this.minValue = builder.minValue();
    this.maxValue = builder.maxValue();
    this.integer = builder.integer();
    this.thresholdReadOnly = builder.thresholdReadOnly();
    this.thresholdConditionOnly = builder.thresholdConditionReadOnly();
    this.thresholdCondition = builder.thresholdCondition();
  }

  public Condition getThresholdCondition() {
    return thresholdCondition;
  }

  @Data
  @Accessors(chain = true, fluent = true)
  private static class UnitBuilder {
    private Measure measure;
    private String displayName = "";
    private String metricName = "";
    private double minValue = 0;
    private double maxValue = Double.MAX_VALUE;
    private boolean integer = false;
    private boolean thresholdReadOnly = false;
    private boolean thresholdConditionReadOnly = false;
    private Condition thresholdCondition = GREATER_THAN;
  }
}
