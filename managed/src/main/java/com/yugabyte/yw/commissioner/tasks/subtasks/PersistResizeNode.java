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
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Date;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PersistResizeNode extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PersistResizeNode.class);

  @Inject
  public PersistResizeNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UserIntent newUserIntent;
    public UUID clusterUUID;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    StringBuilder sb = new StringBuilder(super.getName());
    sb.append('(');
    sb.append(taskParams().getUniverseUUID());
    UserIntent intent = taskParams().newUserIntent;
    sb.append(", instanceType: ");
    sb.append(intent.instanceType);
    if (intent.deviceInfo != null) {
      sb.append(", volumeSize: ");
      sb.append(intent.deviceInfo.volumeSize);
      if (intent.deviceInfo.diskIops != null) {
        sb.append(", volumeIops: ");
        sb.append(intent.deviceInfo.diskIops);
      }
      if (intent.deviceInfo.throughput != null) {
        sb.append(", volumeThroughput: ");
        sb.append(intent.deviceInfo.throughput);
      }
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

              Cluster cluster = universeDetails.getClusterByUuid(taskParams().clusterUUID);
              Set<NodeDetails> nodesInCluster =
                  universe.getUniverseDetails().getNodesInCluster(cluster.uuid);

              UserIntent userIntent = cluster.userIntent;
              UserIntent newUserIntent = taskParams().newUserIntent;
              userIntent.instanceType = newUserIntent.instanceType;
              userIntent.setUserIntentOverrides(newUserIntent.getUserIntentOverrides());
              userIntent.masterInstanceType = newUserIntent.masterInstanceType;
              Date now = new Date();
              DeviceInfo oldDeviceInfo = userIntent.deviceInfo.clone();
              userIntent.deviceInfo.volumeSize = newUserIntent.deviceInfo.volumeSize;
              userIntent.deviceInfo.diskIops = newUserIntent.deviceInfo.diskIops;
              userIntent.deviceInfo.throughput = newUserIntent.deviceInfo.throughput;
              if (!Objects.equals(userIntent.deviceInfo, oldDeviceInfo)) {
                nodesInCluster.stream()
                    .filter(n -> n.isTserver)
                    .forEach(node -> node.lastVolumeUpdateTime = now);
              }
              DeviceInfo oldMasterDeviceInfo = null;
              if (userIntent.masterDeviceInfo != null) {
                oldMasterDeviceInfo = userIntent.masterDeviceInfo.clone();
              }
              if (newUserIntent.masterDeviceInfo != null || userIntent.masterDeviceInfo != null) {
                userIntent.masterDeviceInfo.volumeSize = newUserIntent.masterDeviceInfo.volumeSize;
                userIntent.masterDeviceInfo.diskIops = newUserIntent.masterDeviceInfo.diskIops;
                userIntent.masterDeviceInfo.throughput = newUserIntent.masterDeviceInfo.throughput;
              } else {
                userIntent.masterDeviceInfo = newUserIntent.masterDeviceInfo;
              }
              if (!Objects.equals(oldMasterDeviceInfo, userIntent.masterDeviceInfo)) {
                nodesInCluster.stream()
                    .filter(n -> n.isMaster)
                    .forEach(node -> node.lastVolumeUpdateTime = now);
              }

              for (NodeDetails nodeDetails : nodesInCluster) {
                nodeDetails.disksAreMountedByUUID = true;
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
}
