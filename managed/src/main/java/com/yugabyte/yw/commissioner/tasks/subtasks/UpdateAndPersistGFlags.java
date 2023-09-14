/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateAndPersistGFlags extends UniverseTaskBase {

  @Inject
  protected UpdateAndPersistGFlags(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for setting and persisting universe gflags.
  public static class Params extends UniverseTaskParams {
    public Map<String, String> masterGFlags;
    public Map<String, String> tserverGFlags;
    public List<UUID> clusterUUIDs;
    public SpecificGFlags specificGFlags;
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

              List<Cluster> clusterList;
              if (taskParams().clusterUUIDs != null && taskParams().clusterUUIDs.size() > 0) {
                clusterList =
                    universeDetails.clusters.stream()
                        .filter(c -> taskParams().clusterUUIDs.contains(c.uuid))
                        .collect(Collectors.toList());
              } else {
                clusterList = universeDetails.clusters;
              }
              boolean usingSpecificGFlags = false;
              // Update the gflags.
              for (Cluster cluster : clusterList) {
                cluster.userIntent.specificGFlags = taskParams().specificGFlags;
                cluster.userIntent.masterGFlags = taskParams().masterGFlags;
                cluster.userIntent.tserverGFlags = taskParams().tserverGFlags;
                usingSpecificGFlags =
                    usingSpecificGFlags || cluster.userIntent.specificGFlags != null;
              }
              // Sync gflags to userIntent
              for (Cluster cluster : universeDetails.clusters) {
                Map<String, String> masterGFlags =
                    GFlagsUtil.getBaseGFlags(ServerType.MASTER, cluster, universeDetails.clusters);
                Map<String, String> tserverGFlags =
                    GFlagsUtil.getBaseGFlags(ServerType.TSERVER, cluster, universeDetails.clusters);
                if (usingSpecificGFlags) {
                  // Updating old maps accordingly
                  cluster.userIntent.masterGFlags = masterGFlags;
                  cluster.userIntent.tserverGFlags = tserverGFlags;
                  if (cluster.userIntent.specificGFlags != null
                      && cluster.userIntent.specificGFlags.isInheritFromPrimary()) {
                    SpecificGFlags primaryGFlags =
                        universeDetails.getPrimaryCluster().userIntent.specificGFlags;
                    cluster.userIntent.specificGFlags.setPerProcessFlags(
                        primaryGFlags.getPerProcessFlags());
                    cluster.userIntent.specificGFlags.setPerAZ(primaryGFlags.getPerAZ());
                  }
                }
                GFlagsUtil.syncGflagsToIntent(masterGFlags, cluster.userIntent);
                GFlagsUtil.syncGflagsToIntent(tserverGFlags, cluster.userIntent);
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
