// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
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
import org.apache.commons.lang3.StringUtils;

public class ReplaceRootVolume extends NodeTaskBase {

  @Inject
  protected ReplaceRootVolume(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // For OCI, the caller sets this to the target image ID, and OCI creates the new boot volume
    // from it during the replacement.
    public String replacementDisk;
    // AZ to node name to volume ID.
    public Map<UUID, Map<String, String>> bootDisksPerNodePerZone;
    public String rootDeviceName;
    public Map<UUID, String> rootDevicePerZone;
    public String capacityReservation;
    // OCI only: replace even a boot volume already created from the target image.
    public boolean forceReplacement;
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
    Universe universe = getUniverse();
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    Provider provider = Util.getProviderForNode(nodeDetails, universe);
    boolean isOci = provider.getCloudCode() == CloudType.oci;
    if (!isOci) {
      Map<String, String> bootDisksPerNode = taskParams().bootDisksPerNodePerZone.get(azUuid);
      if (bootDisksPerNode == null || bootDisksPerNode.isEmpty()) {
        throw new IllegalStateException("No available boot disks in AZ " + azUuid.toString());
      }
      taskParams().replacementDisk = bootDisksPerNode.get(taskParams().nodeName);
    }
    if (StringUtils.isBlank(taskParams().replacementDisk)) {
      throw new IllegalStateException(
          "No replacement boot disk or image for node " + taskParams().nodeName);
    }
    taskParams().capacityReservation =
        CapacityReservationUtil.getReservationIfPresent(
            getTaskCache(), provider, taskParams().nodeName);
    // Delete node agent record as the image is going to be replaced.
    deleteNodeAgent(nodeDetails);
    if (taskParams().rootDevicePerZone != null) {
      String rootDeviceName = taskParams().rootDevicePerZone.get(azUuid);
      taskParams().rootDeviceName = rootDeviceName;
    }
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Replace_Root_Volume, taskParams())
        .processErrors();

    // On OCI, replacementDisk is an image, and ybops does not report the new boot volume's ID.
    if (!isOci) {
      saveUniverseDetails(
          u -> {
            NodeDetails node = u.getNode(taskParams().nodeName);
            node.cloudInfo.root_volume = taskParams().replacementDisk;
          });
    }
  }
}
