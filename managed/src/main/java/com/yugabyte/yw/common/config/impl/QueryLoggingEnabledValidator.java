/*
 * Copyright 2024 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
public class QueryLoggingEnabledValidator implements RuntimeConfigPreChangeValidator {
  public String getKeyPath() {
    return "yb.universe.query_logging_enabled";
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    String value = null;
    Optional<RuntimeConfigEntry> runtimeConfigEntry = RuntimeConfigEntry.maybeGet(scopeUUID, path);
    if (runtimeConfigEntry.isPresent()) {
      value = runtimeConfigEntry.get().getValue();
    }

    Set<String> universesWithQueryLoggingEnabled = getUniversesWithQueryLogging();

    if (value != null
        && value.equals("true")
        && newValue.equals("false")
        && CollectionUtils.isNotEmpty(universesWithQueryLoggingEnabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "yb.universe.query_logging_enabled cannot be turned off, since the following universes"
              + " have query logging enabled: "
              + universesWithQueryLoggingEnabled);
    }
  }

  @Override
  public void validateDeleteConfig(UUID scopeUUID, String path) {
    Set<String> universesWithQueryLoggingEnabled = getUniversesWithQueryLogging();
    if (CollectionUtils.isNotEmpty(universesWithQueryLoggingEnabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "yb.universe.query_logging_enabled cannot be reset, since the following universes"
              + " have query logging enabled: "
              + universesWithQueryLoggingEnabled);
    }
  }

  public Set<String> getUniversesWithQueryLogging() {
    // Check if any universe has query logging enabled.
    Set<Universe> universes = Universe.getAllWithoutResources();
    Set<String> universesWithQueryLoggingEnabled =
        universes.stream()
            .filter(
                universe -> {
                  QueryLogConfig queryLogConfig =
                      universe.getUniverseDetails().getPrimaryCluster().userIntent.queryLogConfig;
                  if (queryLogConfig != null
                      && ((queryLogConfig.getYsqlQueryLogConfig() != null
                          && queryLogConfig.getYsqlQueryLogConfig().isEnabled()))) {
                    return true;
                  }
                  return false;
                })
            .map(Universe::getName)
            .collect(Collectors.toSet());

    return universesWithQueryLoggingEnabled;
  }
}
