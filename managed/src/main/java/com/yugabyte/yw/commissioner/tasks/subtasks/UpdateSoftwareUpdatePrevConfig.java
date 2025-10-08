// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateSoftwareUpdatePrevConfig extends UniverseTaskBase {

  @Inject
  protected UpdateSoftwareUpdatePrevConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public boolean canRollbackCatalogUpgrade;
    public boolean allTserversUpgradedToYsqlMajorVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    try {
      // Create the update lambda.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              // If this universe is not being edited, fail the request.
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              if (!universeDetails.updateInProgress) {
                String errMsg =
                    "UserUniverse " + taskParams().getUniverseUUID() + " is not being edited.";
                log.error(errMsg);
                throw new RuntimeException(errMsg);
              }
              universeDetails.prevYBSoftwareConfig.setCanRollbackCatalogUpgrade(
                  taskParams().canRollbackCatalogUpgrade);
              universeDetails.prevYBSoftwareConfig.setAllTserversUpgradedToYsqlMajorVersion(
                  taskParams().allTserversUpgradedToYsqlMajorVersion);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);
      log.info("Completed {}", getName());
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e);
      throw new RuntimeException(msg, e);
    }
  }
}
