// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistKubernetesImmutableYbc;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.KubernetesToggleImmutableYbcParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;

@Abortable
@Retryable
public class KubernetesToggleImmutableYbc extends KubernetesUpgradeTaskBase {

  @Inject
  protected KubernetesToggleImmutableYbc(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.HelmUpgrade;
  }

  @Override
  protected KubernetesToggleImmutableYbcParams taskParams() {
    return (KubernetesToggleImmutableYbcParams) taskParams;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
          String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
          taskParams()
              .getPrimaryCluster()
              .userIntent
              .setUseYbdbInbuiltYbc(taskParams().isUseYbdbInbuiltYbc());

          UpgradeContext upgradeContext =
              UpgradeContext.builder()
                  .useYBDBInbuiltYbc(taskParams().isUseYbdbInbuiltYbc())
                  .build();

          // Create Kubernetes Upgrade Task.
          switch (taskParams().upgradeOption) {
            case ROLLING_UPGRADE:
              createUpgradeTask(
                  universe,
                  cluster.userIntent.ybSoftwareVersion,
                  // Explicity set upgradeMasters to false, master partition will be non-zero after
                  // task completes
                  // to avoid unexpected restarts.
                  false /* upgradeMasters */,
                  true /* upgradeTservers */,
                  universe.isYbcEnabled(),
                  stableYbcVersion,
                  upgradeContext);
              break;
            case NON_ROLLING_UPGRADE:
              createNonRollingUpgradeTask(
                  universe,
                  cluster.userIntent.ybSoftwareVersion,
                  // Explicity set upgradeMasters to false, master partition will be non-zero after
                  // task completes
                  // to avoid unexpected restarts.
                  false /* upgradeMasters */,
                  true /* upgradeTservers */,
                  universe.isYbcEnabled(),
                  stableYbcVersion,
                  upgradeContext);
              break;
            default:
              throw new RuntimeException("Unsupported upgrade option!");
          }
          // Persist in the DB.
          addPersistKubernetesImmutableYbc();
        });
  }

  private SubTaskGroup addPersistKubernetesImmutableYbc() {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup(
            "UpdateAndPersistKubernetesImmutableYbc", SubTaskGroupType.PersistYbdbInbuiltYbc);
    UpdateAndPersistKubernetesImmutableYbc.Params params =
        new UpdateAndPersistKubernetesImmutableYbc.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.useYbdbInbuiltYbc = taskParams().isUseYbdbInbuiltYbc();
    UpdateAndPersistKubernetesImmutableYbc task =
        createTask(UpdateAndPersistKubernetesImmutableYbc.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
