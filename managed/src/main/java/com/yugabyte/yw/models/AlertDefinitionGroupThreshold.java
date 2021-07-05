/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
public class AlertDefinitionGroupThreshold {

  public enum Condition {
    GREATER_THAN(">"),
    LESS_THAN("<");

    private final String value;

    Condition(String value) {
      this.value = value;
    }

    public String getValue() {
      return value;
    }
  }

  private Condition condition;
  private double threshold;
}
