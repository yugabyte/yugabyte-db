// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateAndPersistKubernetesImmutableYbc extends UniverseTaskBase {

  @Inject
  protected UpdateAndPersistKubernetesImmutableYbc(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for setting and persisting useYbdbInbuiltYbc
  public static class Params extends UniverseTaskParams {
    public boolean useYbdbInbuiltYbc;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);

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
              if (!taskParams().useYbdbInbuiltYbc) {
                universeDetails.setYbcSoftwareVersion(stableYbcVersion);
              } else {
                universeDetails.setYbcSoftwareVersion(null /* ybcSoftwareVersion */);
              }
              UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
              userIntent.setUseYbdbInbuiltYbc(taskParams().useYbdbInbuiltYbc);
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
