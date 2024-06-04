/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import static com.yugabyte.yw.common.SwamperHelper.COLLECTION_LEVEL_PARAM;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import javax.inject.Singleton;

@Singleton
public class MetricCollectionLevelValidator implements RuntimeConfigPreChangeValidator {

  public String getKeyPath() {
    return COLLECTION_LEVEL_PARAM;
  }

  @Override
  public void validateAny(String path, String newValue) {
    try {
      MetricCollectionLevel.fromString(newValue);
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid metric collection level: " + newValue);
    }
  }
}
