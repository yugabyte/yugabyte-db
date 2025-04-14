// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.MetricSourceState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class PauseKubernetesUniverse extends KubernetesUpgradeTaskBase {

  @Inject
  protected PauseKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  @JsonDeserialize(converter = Params.Converter.class)
  public static class Params extends UpgradeTaskParams {
    public UUID customerUUID;

    public static class Converter
        extends UpgradeTaskParams.BaseConverter<PauseKubernetesUniverse.Params> {}
  }

  @Override
  protected Params taskParams() {
    return (Params) super.taskParams();
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.PauseUniverse;
  }

  public UserTaskDetails.SubTaskGroupType getTaskDetails() {
    return UserTaskDetails.SubTaskGroupType.PauseUniverse;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    try {
      Universe universe = lockAndFreezeUniverseForUpdate(-1, null);
      kubernetesStatus.startYBUniverseEventStatus(
          universe,
          taskParams().getKubernetesResourceDetails(),
          TaskType.PauseUniverse.name(),
          getUserTaskUUID(),
          UniverseState.PAUSING);
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;
      taskParams().nodePrefix = universe.getUniverseDetails().nodePrefix;
      // Pause the kubernetes universe
      createPauseKubernetesUniverseTasks(universe.getName());
      // Update swamper targets, metrics, and alerts
      createSwamperTargetUpdateTask(false);
      createUnivManageAlertDefinitionsTask(false)
          .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      createMarkSourceMetricsTask(universe, MetricSourceState.INACTIVE)
          .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      // Mark the universe as paused
      createUpdateUniverseFieldsTask(
          u -> {
            UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
            universeDetails.universePaused = true;
            u.setUniverseDetails(universeDetails);
          });
      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.PauseUniverse.name(),
          getUserTaskUUID(),
          UniverseState.PAUSED,
          null);
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.PauseUniverse.name(),
          getUserTaskUUID(),
          UniverseState.ERROR_PAUSING,
          t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
