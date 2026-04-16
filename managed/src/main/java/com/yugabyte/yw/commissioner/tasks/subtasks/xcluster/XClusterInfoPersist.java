package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class XClusterInfoPersist extends UniverseTaskBase {

  @Inject
  protected XClusterInfoPersist(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    // The universe UUID must be stored in universeUUID field.
    // The xCluster info object to persist.
    public UniverseDefinitionTaskParams.XClusterInfo xClusterInfo;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUUID=%s,xClusterInfo=%s)",
        super.getName(), taskParams().getUniverseUUID(), taskParams().xClusterInfo);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    try {
      // Create the update lambda.
      Universe.UniverseUpdater updater =
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

            // If this universe is not being updated, fail the request.
            if (!universeDetails.updateInProgress) {
              String msg =
                  "UserUniverse " + taskParams().getUniverseUUID() + " is not being updated.";
              log.error(msg);
              throw new RuntimeException(msg);
            }

            // Update the xClusterInfo.
            universeDetails.xClusterInfo = taskParams().xClusterInfo;

            universe.setUniverseDetails(universeDetails);
          };

      // Perform the update.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
