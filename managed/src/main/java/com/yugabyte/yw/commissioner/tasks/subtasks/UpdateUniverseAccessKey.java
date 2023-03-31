package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateUniverseAccessKey extends UniverseTaskBase {

  @Inject
  protected UpdateUniverseAccessKey(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public String newAccessKeyCode;
    public UUID clusterUUID;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.getClusterByUuid(taskParams().clusterUUID).userIntent.accessKeyCode =
                  taskParams().newAccessKeyCode;
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
