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
    public Integer volumeIops;
    public Integer volumeThroughput;
    public List<UUID> clusters;
    public String masterInstanceType;
    public Integer masterVolumeSize;
    public Integer masterVolumeIops;
    public Integer masterVolumeThroughput;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    StringBuilder sb = new StringBuilder(super.getName());
    sb.append('(');
    sb.append(taskParams().getUniverseUUID());
    sb.append(", instanceType: ");
    sb.append(taskParams().instanceType);
    if (taskParams().volumeSize != null) {
      sb.append(", volumeSize: ");
      sb.append(taskParams().volumeSize);
    }
    if (taskParams().volumeIops != null) {
      sb.append(", volumeIops: ");
      sb.append(taskParams().volumeIops);
    }
    if (taskParams().volumeThroughput != null) {
      sb.append(", volumeThroughput: ");
      sb.append(taskParams().volumeThroughput);
    }
    sb.append(')');
    return sb.toString();
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
                if (taskParams().volumeIops != null) {
                  userIntent.deviceInfo.diskIops = taskParams().volumeIops;
                }
                if (taskParams().volumeThroughput != null) {
                  userIntent.deviceInfo.throughput = taskParams().volumeThroughput;
                }
                if (taskParams().masterVolumeSize != null) {
                  userIntent.masterDeviceInfo.volumeSize = taskParams().masterVolumeSize;
                }
                if (taskParams().masterVolumeIops != null) {
                  userIntent.masterDeviceInfo.diskIops = taskParams().masterVolumeIops;
                }
                if (taskParams().masterVolumeThroughput != null) {
                  userIntent.masterDeviceInfo.throughput = taskParams().masterVolumeThroughput;
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
