// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateClusterAPIDetails extends UniverseTaskBase {

  @Inject
  protected UpdateClusterAPIDetails(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public boolean enableYSQL = false;
    public boolean enableConnectionPooling = false;
    public Map<UUID, SpecificGFlags> connectionPoolingGflags = new HashMap<>();
    public boolean enableYSQLAuth = false;
    public boolean enableYCQL = false;
    public boolean enableYCQLAuth = false;
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
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              // If this universe is not being updated, fail the request.
              if (!universeDetails.updateInProgress) {
                String msg =
                    "UserUniverse " + taskParams().getUniverseUUID() + " is not being updated.";
                log.error(msg);
                throw new RuntimeException(msg);
              }

              for (Cluster cluster : universeDetails.clusters) {
                cluster.userIntent.enableYSQL = taskParams().enableYSQL;
                cluster.userIntent.enableConnectionPooling = taskParams().enableConnectionPooling;
                if (taskParams().connectionPoolingGflags.containsKey(cluster.uuid)) {
                  SpecificGFlags clusterConnectionPoolingGFlags =
                      taskParams().connectionPoolingGflags.get(cluster.uuid);
                  // Update the old tserver and master gflag fields for backward compatibility.
                  cluster.userIntent.tserverGFlags.putAll(
                      clusterConnectionPoolingGFlags
                          .getPerProcessFlags()
                          .value
                          .getOrDefault(ServerType.TSERVER, new HashMap<>()));
                  cluster.userIntent.masterGFlags.putAll(
                      clusterConnectionPoolingGFlags
                          .getPerProcessFlags()
                          .value
                          .getOrDefault(ServerType.MASTER, new HashMap<>()));

                  // Update the specific gflags.
                  cluster.userIntent.specificGFlags =
                      SpecificGFlags.combine(
                          cluster.userIntent.specificGFlags, clusterConnectionPoolingGFlags);
                }
                cluster.userIntent.enableYSQLAuth = taskParams().enableYSQLAuth;
                cluster.userIntent.enableYCQL = taskParams().enableYCQL;
                cluster.userIntent.enableYCQLAuth = taskParams().enableYCQLAuth;
              }

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
