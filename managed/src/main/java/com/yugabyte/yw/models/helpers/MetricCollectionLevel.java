/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.helpers;

import lombok.Getter;
import org.apache.commons.lang3.StringUtils;

@Getter
public enum MetricCollectionLevel {
  ALL(StringUtils.EMPTY, false),
  NORMAL("metric/normal_level_params.json", false),
  MINIMAL("metric/minimal_level_params.json", false),
  OFF(StringUtils.EMPTY, true);

  private final String paramsFilePath;
  private final boolean disableCollection;

  MetricCollectionLevel(String paramsFilePath, boolean disableCollection) {
    this.paramsFilePath = paramsFilePath;
    this.disableCollection = disableCollection;
  }

  public static MetricCollectionLevel fromString(String level) {
    if (level == null) {
      throw new IllegalArgumentException("Level can't be null");
    }
    return MetricCollectionLevel.valueOf(level.toUpperCase());
  }
}
