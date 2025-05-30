/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import java.util.UUID;
import javax.inject.Singleton;

@Singleton
public class HcvTokenRenewPercentValidator implements RuntimeConfigPreChangeValidator {
  public String getKeyPath() {
    return GlobalConfKeys.hcvTokenRenewPercent.getKey();
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    Integer newValueInt = Integer.parseInt(newValue);

    // Check that the runtime config value is between 0 and 100.
    if (newValueInt == null || newValueInt <= 0 || newValueInt >= 100) {
      throw new PlatformServiceException(
          BAD_REQUEST, "HCV token renew percent must be between 0 and 100.");
    }
  }
}
