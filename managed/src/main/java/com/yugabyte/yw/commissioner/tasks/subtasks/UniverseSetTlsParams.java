// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;
import java.util.UUID;

@Slf4j
public class UniverseSetTlsParams extends UniverseTaskBase {

  @Inject
  protected UniverseSetTlsParams(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public boolean enableNodeToNodeEncrypt;
    public boolean enableClientToNodeEncrypt;
    public boolean allowInsecure;
    public UUID rootCA;
  }

  protected UniverseSetTlsParams.Params taskParams() {
    return (UniverseSetTlsParams.Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName();
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      // Create the update lambda.
      Universe.UniverseUpdater updater =
          universe -> {
            // If this universe is not being edited, fail the request.
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            if (!universeDetails.updateInProgress) {
              String errMsg = "UserUniverse " + taskParams().universeUUID + " is not being edited.";
              log.error(errMsg);
              throw new RuntimeException(errMsg);
            }

            UniverseDefinitionTaskParams.UserIntent userIntent =
                universeDetails.getPrimaryCluster().userIntent;
            userIntent.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
            userIntent.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
            universeDetails.allowInsecure = taskParams().allowInsecure;
            universeDetails.rootCA = null;
            if (taskParams().enableNodeToNodeEncrypt || taskParams().enableClientToNodeEncrypt) {
              universeDetails.rootCA = taskParams().rootCA;
            }
            universe.setUniverseDetails(universeDetails);
          };

      // Perform the update. If unsuccessful, this will throw a runtime
      // exception which we do not catch as we want to fail.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
