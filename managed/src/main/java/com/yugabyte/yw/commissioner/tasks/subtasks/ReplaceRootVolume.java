// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;

public class ReplaceRootVolume extends NodeTaskBase {

  @Inject
  protected ReplaceRootVolume(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public String replacementDisk;
    // AZ to node name to volume ID.
    public Map<UUID, Map<String, String>> bootDisksPerNodePerZone;
    public String rootDeviceName;
    public Map<UUID, String> rootDevicePerZone;
    public String capacityReservation;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID azUuid = taskParams().azUuid;
    if (azUuid == null) {
      throw new IllegalStateException("AZ must not be null");
    }

    Map<String, String> bootDisksPerNode = taskParams().bootDisksPerNodePerZone.get(azUuid);

    if (bootDisksPerNode == null || bootDisksPerNode.isEmpty()) {
      throw new IllegalStateException("No available boot disks in AZ " + azUuid.toString());
    }
    Universe universe = getUniverse();
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    Provider provider = Util.getProviderForNode(nodeDetails, universe);
    taskParams().capacityReservation =
        CapacityReservationUtil.getReservationIfPresent(
            getTaskCache(), provider, taskParams().nodeName);
    // Delete node agent record as the image is going to be replaced.
    deleteNodeAgent(nodeDetails);
    taskParams().replacementDisk = bootDisksPerNode.get(taskParams().nodeName);
    if (taskParams().rootDevicePerZone != null) {
      String rootDeviceName = taskParams().rootDevicePerZone.get(azUuid);
      taskParams().rootDeviceName = rootDeviceName;
    }
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Replace_Root_Volume, taskParams())
        .processErrors();

    saveUniverseDetails(
        u -> {
          NodeDetails node = u.getNode(taskParams().nodeName);
          node.cloudInfo.root_volume = taskParams().replacementDisk;
        });
  }
}
