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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class SSH2EnabledKeyValidator implements RuntimeConfigPreChangeValidator {
  private static final Logger LOG = LoggerFactory.getLogger(SSH2EnabledKeyValidator.class);

  public String getKeyPath() {
    return "yb.security.ssh2_enabled";
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {

    String value = null;
    Optional<RuntimeConfigEntry> runtimeConfigEntry = RuntimeConfigEntry.maybeGet(scopeUUID, path);
    if (runtimeConfigEntry.isPresent()) {
      value = runtimeConfigEntry.get().getValue();
    }

    if (value != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.security.ssh2_enabled cannot be reinitialised");
    }
  }

  @Override
  public void validateDeleteConfig(UUID scopeUUID, String path) {
    throw new PlatformServiceException(BAD_REQUEST, "Cannot delete 'yb.security.ssh2_enabled'.");
  }
}
