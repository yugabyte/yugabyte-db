/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *     POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateAndPersistKubernetesOverrides extends UniverseTaskBase {

  @Inject
  protected UpdateAndPersistKubernetesOverrides(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for setting and persisting k8s overrides.
  public static class Params extends UniverseTaskParams {
    public String universeOverrides;
    public Map<String, String> azOverrides;
  }

  protected Params taskParams() {
    return (Params) taskParams;
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
                    "UserUniverse " + taskParams().getUniverseUUID() + " is not being updated.";
                log.error(msg);
                throw new RuntimeException(msg);
              }

              // Update the overrides.
              UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
              userIntent.universeOverrides = taskParams().universeOverrides;
              userIntent.azOverrides = taskParams().azOverrides;

              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.error(msg, e);
      throw new RuntimeException(msg, e);
    }
  }
}
