// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PersistResizeNode extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PersistResizeNode.class);

  @Inject
  public PersistResizeNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public String instanceType;
    public Integer volumeSize;
    public List<UUID> clusters;
    public String masterInstanceType;
    public Integer masterVolumeSize;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    String ret =
        super.getName()
            + "("
            + taskParams().universeUUID
            + ", instanceType: "
            + taskParams().instanceType;
    if (taskParams().volumeSize != null) {
      ret += ", volumeSize: " + taskParams().volumeSize;
    }
    return ret + ")";
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

              for (Cluster cluster : getClustersToUpdate(universeDetails)) {
                UserIntent userIntent = cluster.userIntent;
                userIntent.instanceType = taskParams().instanceType;
                if (taskParams().masterInstanceType != null) {
                  userIntent.masterInstanceType = taskParams().masterInstanceType;
                }
                if (taskParams().volumeSize != null) {
                  userIntent.deviceInfo.volumeSize = taskParams().volumeSize;
                }
                if (taskParams().masterVolumeSize != null) {
                  userIntent.masterDeviceInfo.volumeSize = taskParams().masterVolumeSize;
                }
                for (NodeDetails nodeDetails :
                    universe.getUniverseDetails().getNodesInCluster(cluster.uuid)) {
                  nodeDetails.disksAreMountedByUUID = true;
                }
              }

              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);

    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }

  private List<Cluster> getClustersToUpdate(UniverseDefinitionTaskParams universeDetails) {
    List<UUID> paramsClusters = taskParams().clusters;
    if (paramsClusters == null || paramsClusters.isEmpty()) {
      return universeDetails.clusters;
    }
    return universeDetails
        .clusters
        .stream()
        .filter(c -> paramsClusters.contains(c.uuid))
        .collect(Collectors.toList());
  }
}
