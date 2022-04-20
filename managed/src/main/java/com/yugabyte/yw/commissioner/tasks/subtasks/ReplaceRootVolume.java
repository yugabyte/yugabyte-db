package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;

public class ReplaceRootVolume extends NodeTaskBase {

  @Inject
  protected ReplaceRootVolume(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {
    public String replacementDisk;
    public Map<UUID, List<String>> bootDisksPerZone;
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

    List<String> bootDisks = taskParams().bootDisksPerZone.get(azUuid);

    if (bootDisks == null || bootDisks.isEmpty()) {
      throw new IllegalStateException("No available boot disks in AZ " + azUuid.toString());
    }

    // this won't be saved in taskDetails!
    taskParams().replacementDisk = bootDisks.remove(0);
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Replace_Root_Volume, taskParams())
        .processErrors();
  }
}
