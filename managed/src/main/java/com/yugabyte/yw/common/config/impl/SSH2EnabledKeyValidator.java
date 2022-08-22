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

import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.RuntimeConfigEntry;

import java.util.UUID;
import javax.inject.Singleton;
import static play.mvc.Http.Status.BAD_REQUEST;

@Singleton
public class SSH2EnabledKeyValidator implements RuntimeConfigPreChangeValidator {

  public String getKeyPath() {
    return "yb.security.ssh2_enabled";
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    RuntimeConfigEntry runtimeConfigEntry = RuntimeConfigEntry.getOrBadRequest(scopeUUID, path);

    String value = runtimeConfigEntry.getValue();
    if (value != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.security.ssh2_enabled cannot be reinitialised");
    }
  }
}
