// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.YbcGflagsTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class UpgradeYbcGFlags extends UniverseTaskBase {

  private final YbcManager ybcManager;

  @Override
  protected YbcGflagsTaskParams taskParams() {
    return (YbcGflagsTaskParams) taskParams;
  }

  @Inject
  protected UpgradeYbcGFlags(BaseTaskDependencies baseTaskDependencies, YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcManager = ybcManager;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Map<String, String> ybcGflagsMap = null;
    try {
      ybcGflagsMap = YbcManager.convertYbcFlagsToMap(taskParams().ybcGflags);
    } catch (IllegalAccessException e) {
      throw new RuntimeException(e);
    }

    String errorString = null;
    try {
      lockAndFreezeUniverseForUpdate(
          universe.getUniverseUUID(), universe.getVersion(), null /* firstRunTxnCallback */);
      for (NodeDetails node : universe.getTServers()) {
        AnsibleConfigureServers.Params params =
            ybcManager.getAnsibleConfigureYbcServerTaskParams(
                universe,
                node,
                ybcGflagsMap,
                UpgradeTaskType.YbcGFlags,
                UpgradeTaskSubType.YbcGflagsUpdate);
        getAnsibleConfigureYbcServerTasks(params, universe)
            .setSubTaskGroupType(SubTaskGroupType.UpdatingYbcGFlags);
      }
      List<NodeDetails> nodeDetailsList = new ArrayList<>(universe.getTServers());
      createServerControlTasks(nodeDetailsList, ServerType.CONTROLLER, "stop")
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      createServerControlTasks(nodeDetailsList, ServerType.CONTROLLER, "start")
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      createWaitForYbcServerTask(nodeDetailsList)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      createUpdateYbcGFlagInTheUniverseDetailsTask(ybcGflagsMap)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Mark universe update succeeded
      createMarkUniverseUpdateSuccessTasks(universe.getUniverseUUID())
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      errorString = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate(taskParams().getUniverseUUID(), errorString);
    }
    log.info("Finished {} task.", getName());
  }
}
