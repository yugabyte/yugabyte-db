package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;

public class ReplaceRootVolume extends NodeTaskBase {
  private final Map<UUID, List<String>> bootDisksPerZone;

  public ReplaceRootVolume(Map<UUID, List<String>> bootDisksPerZone) {
    this.bootDisksPerZone = bootDisksPerZone;
  }

  public static class Params extends NodeTaskParams {
    public String replacementDisk;
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

    List<String> bootDisks = bootDisksPerZone.get(azUuid);

    if (bootDisks == null || bootDisks.isEmpty()) {
      throw new IllegalStateException("No available boot disks in AZ " + azUuid.toString());
    }

    // this won't be saved in taskDetails!
    taskParams().replacementDisk = bootDisks.remove(0);
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Replace_Root_Volume, taskParams());
    processShellResponse(response);
  }
}
