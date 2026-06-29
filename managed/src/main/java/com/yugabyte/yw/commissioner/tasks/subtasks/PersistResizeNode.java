// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Date;
import java.util.HashSet;
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
    public boolean onlyPersistDeviceInfo;
    public Set<UUID> skipMasterAZs;
    public Set<UUID> skipTserverAZs;
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

              Date now = new Date();
              UserIntent userIntent = cluster.userIntent;
              UserIntent newUserIntent = taskParams().newUserIntent;

              for (NodeDetails nodeDetails : nodesInCluster) {
                DeviceInfo oldDeviceInfo = userIntent.getDeviceInfoForNode(nodeDetails);
                DeviceInfo newDeviceInfo = newUserIntent.getDeviceInfoForNode(nodeDetails);
                if (!Objects.equals(newDeviceInfo, oldDeviceInfo)) {
                  nodeDetails.lastVolumeUpdateTime = now;
                }
                nodeDetails.disksAreMountedByUUID = true;
              }
              if (userIntent.isMulticloudSupport()) {
                Set<String> skipTserverAzCodes = new HashSet<>();
                Set<String> skipMasterAzCodes = new HashSet<>();
                initCodes(skipTserverAzCodes, skipMasterAzCodes);
                Util.mergeProviderSpecifications(
                    userIntent,
                    newUserIntent,
                    ctx -> {
                      Set<String> skipCodes =
                          ctx.getServerType() == ServerType.TSERVER
                              ? skipTserverAzCodes
                              : skipMasterAzCodes;
                      if (!skipCodes.contains(ctx.getTraversePath().getAzCode())) {
                        ctx.getCurrent()
                            .setDeviceInfo(
                                mergeDeviceInfos(
                                    ctx.getCurrent().getDeviceInfo(),
                                    ctx.getSource().getDeviceInfo()));
                        if (!taskParams().onlyPersistDeviceInfo) {
                          ctx.getCurrent().setInstanceType(ctx.getSource().getInstanceType());
                          ctx.getCurrent().setCgroupSize(ctx.getSource().getCgroupSize());
                        }
                      }
                    });
              } else {
                if (!taskParams().onlyPersistDeviceInfo) {
                  userIntent.instanceType = newUserIntent.instanceType;
                  userIntent.setUserIntentOverrides(newUserIntent.getUserIntentOverrides());
                  userIntent.setCgroupSize(newUserIntent.getCgroupSize());
                  userIntent.masterInstanceType = newUserIntent.masterInstanceType;
                }
                userIntent.deviceInfo.volumeSize = newUserIntent.deviceInfo.volumeSize;
                userIntent.deviceInfo.diskIops = newUserIntent.deviceInfo.diskIops;
                userIntent.deviceInfo.throughput = newUserIntent.deviceInfo.throughput;
                if (newUserIntent.masterDeviceInfo != null || userIntent.masterDeviceInfo != null) {
                  userIntent.masterDeviceInfo.volumeSize =
                      newUserIntent.masterDeviceInfo.volumeSize;
                  userIntent.masterDeviceInfo.diskIops = newUserIntent.masterDeviceInfo.diskIops;
                  userIntent.masterDeviceInfo.throughput =
                      newUserIntent.masterDeviceInfo.throughput;
                } else {
                  userIntent.masterDeviceInfo = newUserIntent.masterDeviceInfo;
                }

                // Update AZ volume overrides
                Set<UUID> azUUIDs = cluster.placementInfo.getAllAZUUIDs();
                userIntent.updateAZVolumeOverrides(
                    newUserIntent,
                    azUUIDs,
                    taskParams().skipMasterAZs,
                    true /* isDedicatedMaster */);
                userIntent.updateAZVolumeOverrides(
                    newUserIntent,
                    azUUIDs,
                    taskParams().skipTserverAZs,
                    false /* isDedicatedMaster */);
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

  private void initCodes(Set<String> skipTserverAzCodes, Set<String> skipMasterAzCodes) {
    if (taskParams().skipTserverAZs != null) {
      taskParams().skipTserverAZs.stream()
          .map(uuid -> AvailabilityZone.getOrBadRequest(uuid).getCode())
          .forEach(skipTserverAzCodes::add);
    }
    if (taskParams().skipMasterAZs != null) {
      taskParams().skipMasterAZs.stream()
          .map(uuid -> AvailabilityZone.getOrBadRequest(uuid).getCode())
          .forEach(skipMasterAzCodes::add);
    }
  }

  private DeviceInfo mergeDeviceInfos(DeviceInfo current, DeviceInfo newDevice) {
    if (current == null && newDevice == null) {
      return null;
    }
    if (current == null) {
      return newDevice.clone();
    }
    if (newDevice != null) {
      current.volumeSize = newDevice.volumeSize;
      current.diskIops = newDevice.diskIops;
      current.throughput = newDevice.throughput;
    }
    return current;
  }
}
