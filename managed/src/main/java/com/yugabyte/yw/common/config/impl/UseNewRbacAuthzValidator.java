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
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Singleton;

@Singleton
public class UseNewRbacAuthzValidator implements RuntimeConfigPreChangeValidator {
  public String getKeyPath() {
    return "yb.rbac.use_new_authz";
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    String value = null;
    Optional<RuntimeConfigEntry> runtimeConfigEntry = RuntimeConfigEntry.maybeGet(scopeUUID, path);
    if (runtimeConfigEntry.isPresent()) {
      value = runtimeConfigEntry.get().getValue();
    }

    // Don't allow changing this runtime config from true -> false.
    if (value != null && value.equals("true") && newValue.equals("false")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.rbac.use_new_authz cannot be reinitialised.");
    }
  }
}
