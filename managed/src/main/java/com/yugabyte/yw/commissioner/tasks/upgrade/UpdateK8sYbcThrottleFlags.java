package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.MastersAndTservers;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.YbcThrottleTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class UpdateK8sYbcThrottleFlags extends KubernetesUpgradeTaskBase {

  private final YbcManager ybcManager;

  @Inject
  protected UpdateK8sYbcThrottleFlags(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      YbcManager ybcManager) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
    this.ybcManager = ybcManager;
  }

  @Override
  protected YbcThrottleTaskParams taskParams() {
    return (YbcThrottleTaskParams) taskParams;
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return null; /* non-restart upgrade */
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingYbcGFlags;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    runUpgrade(
        () -> {
          Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
          PreInMemoryApplyTask preInMemoryApplyTask = null;
          if (universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
            preInMemoryApplyTask =
                (u, ybcGflagsMap) -> {
                  taskParams().getPrimaryCluster().userIntent.ybcFlags = ybcGflagsMap;
                  createNonRestartUpgradeTask(u);
                };
          }
          createSetYbcThrottleParamsSubTasks(
              universe, taskParams().getYbcThrottleParameters(), ybcManager, preInMemoryApplyTask);
        });
  }
}
