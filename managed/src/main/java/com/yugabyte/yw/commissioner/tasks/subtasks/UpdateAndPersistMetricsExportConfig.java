/*
 * Copyright 2023 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.forms.MetricsExportConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateAndPersistMetricsExportConfig extends UniverseTaskBase {

  @Inject
  protected UpdateAndPersistMetricsExportConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected MetricsExportConfigParams taskParams() {
    return (MetricsExportConfigParams) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              // If this universe is not being updated, fail the request.
              if (!universeDetails.updateInProgress) {
                String msg =
                    "Universe " + taskParams().getUniverseUUID() + " is not being updated.";
                log.error(msg);
                throw new RuntimeException(msg);
              }
              universeDetails.otelCollectorEnabled =
                  universeDetails.otelCollectorEnabled || taskParams().installOtelCollector;
              universeDetails.getPrimaryCluster().userIntent.metricsExportConfig =
                  taskParams().metricsExportConfig;
              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
