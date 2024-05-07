package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class UpdateClusterUserIntent extends UniverseTaskBase {

  @Inject
  protected UpdateClusterUserIntent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public UUID clusterUUID;
    public UUID imageBundleUUID;
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

              universeDetails.clusters.forEach(
                  (cluster) -> {
                    if (cluster.uuid.equals(taskParams().clusterUUID)) {
                      // Update the imageBundle reference for the cluster in which node
                      // is provisioned.
                      cluster.userIntent.imageBundleUUID = taskParams().imageBundleUUID;
                      universeDetails.nodeDetailsSet.stream()
                          .forEach(
                              nodeDetail -> {
                                // YBM use case where the cluster would have been deployed using the
                                // machineImage
                                // but patched using the imageBundle. It will be good if clear the
                                // machineImage from
                                // nodeDetails.
                                if (nodeDetail.placementUuid.equals(cluster.uuid)
                                    && StringUtils.isNotEmpty(nodeDetail.machineImage)) {
                                  nodeDetail.machineImage = null;
                                }
                              });
                    }
                  });
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
