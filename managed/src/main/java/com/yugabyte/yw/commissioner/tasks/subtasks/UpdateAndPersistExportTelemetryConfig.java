/*
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.ExportTelemetryConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateAndPersistExportTelemetryConfig extends UniverseTaskBase {

  @Inject
  protected UpdateAndPersistExportTelemetryConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected ExportTelemetryConfigParams taskParams() {
    return (ExportTelemetryConfigParams) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      // 1. Create or update export_telemetry_config table (source of truth)
      ExportTelemetryConfigParams params = taskParams();
      TelemetryConfig telemetryConfig =
          params.getTelemetryConfig() != null ? params.getTelemetryConfig() : new TelemetryConfig();

      ExportTelemetryConfig exportTelemetryConfig =
          ExportTelemetryConfig.getForUniverse(params.getUniverseUUID())
              .orElseGet(
                  () -> {
                    ExportTelemetryConfig etc = new ExportTelemetryConfig();
                    etc.setUniverseUuid(params.getUniverseUUID());
                    etc.setTelemetryConfig(new TelemetryConfig());
                    return etc;
                  });
      exportTelemetryConfig.setTelemetryConfig(telemetryConfig);

      // 2. Save export_telemetry_config and sync universe details in one transaction (inside
      // updater).
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              exportTelemetryConfig.save();
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              if (!universeDetails.updateInProgress) {
                String msg =
                    "Universe " + taskParams().getUniverseUUID() + " is not being updated.";
                log.error(msg);
                throw new RuntimeException(msg);
              }
              universeDetails.otelCollectorEnabled =
                  true; // unified flow always enables OTEL collector
              universeDetails.getPrimaryCluster().userIntent.auditLogConfig =
                  params.getAuditLogConfig();
              universeDetails.getPrimaryCluster().userIntent.queryLogConfig =
                  params.getQueryLogConfig();
              universeDetails.getPrimaryCluster().userIntent.metricsExportConfig =
                  params.getMetricsExportConfig();
              universe.setUniverseDetails(universeDetails);
            }
          };
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
