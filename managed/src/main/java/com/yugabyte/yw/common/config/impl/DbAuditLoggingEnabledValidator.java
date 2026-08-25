/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
public class DbAuditLoggingEnabledValidator implements RuntimeConfigPreChangeValidator {
  @Inject private Config staticConfig;

  public String getKeyPath() {
    return "yb.universe.audit_logging_enabled";
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    if (!isEnabled(scopeUUID, path) || Boolean.parseBoolean(newValue)) {
      return;
    }

    Set<String> universesWithDbAuditLoggingEnabled = getUniversesWithDbAuditLogging();

    if (CollectionUtils.isNotEmpty(universesWithDbAuditLoggingEnabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "yb.universe.audit_logging_enabled cannot be turned off, since the following universes"
              + " have DB audit logging enabled: "
              + universesWithDbAuditLoggingEnabled);
    }
  }

  @Override
  public void validateDeleteConfig(UUID scopeUUID, String path) {
    // A reset is a set to the application.conf default.
    validateConfigGlobal(scopeUUID, path, String.valueOf(staticConfig.getBoolean(path)));
  }

  // An absent runtime config row means the key still holds its application.conf default, so
  // absence reads as "enabled" for keys that default to true. Treating it as "not set to true"
  // would silently skip this guard on any install that never wrote the row.
  private boolean isEnabled(UUID scopeUUID, String path) {
    return RuntimeConfigEntry.maybeGet(scopeUUID, path)
        .map(entry -> Boolean.parseBoolean(entry.getValue()))
        .orElseGet(() -> staticConfig.getBoolean(path));
  }

  public Set<String> getUniversesWithDbAuditLogging() {
    // Check if any universe has DB audit logging enabled.
    Set<Universe> universes = Universe.getAllWithoutResources();
    Set<String> universesWithDbAuditLoggingEnabled =
        universes.stream()
            .filter(
                universe -> {
                  AuditLogConfig auditLogConfig =
                      universe.getUniverseDetails().getPrimaryCluster().userIntent.auditLogConfig;
                  if (auditLogConfig != null
                      && ((auditLogConfig.getYsqlAuditConfig() != null
                              && auditLogConfig.getYsqlAuditConfig().isEnabled())
                          || (auditLogConfig.getYcqlAuditConfig() != null
                              && auditLogConfig.getYcqlAuditConfig().isEnabled()))) {
                    return true;
                  }
                  return false;
                })
            .map(Universe::getName)
            .collect(Collectors.toSet());

    return universesWithDbAuditLoggingEnabled;
  }
}
