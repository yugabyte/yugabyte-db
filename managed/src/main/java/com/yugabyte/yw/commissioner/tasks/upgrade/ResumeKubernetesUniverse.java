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
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class ResumeKubernetesUniverse extends KubernetesUpgradeTaskBase {

  @Inject
  protected ResumeKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.ResumeUniverse;
  }

  public UserTaskDetails.SubTaskGroupType getTaskDetails() {
    return UserTaskDetails.SubTaskGroupType.ResumeUniverse;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    addBasicPrecheckTasks();
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  @JsonDeserialize(converter = Params.Converter.class)
  public static class Params extends UpgradeTaskParams {
    public UUID customerUUID;

    public static class Converter
        extends UpgradeTaskParams.BaseConverter<ResumeKubernetesUniverse.Params> {}
  }

  @Override
  protected Params taskParams() {
    return (Params) super.taskParams();
  }

  // Ignore cluster consistency check for Kubernetes.
  // Pause and resume tasks are independent of the universe state.
  @Override
  protected void addBasicPrecheckTasks() {
    return;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockAndFreezeUniverseForUpdate(-1, null /* Txn callback */);
      kubernetesStatus.startYBUniverseEventStatus(
          universe,
          taskParams().getKubernetesResourceDetails(),
          TaskType.ResumeUniverse.name(),
          getUserTaskUUID(),
          UniverseState.RESUMING);
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      // Get these from the universe details
      taskParams().useNewHelmNamingStyle = universeDetails.useNewHelmNamingStyle;
      taskParams().nodePrefix = universeDetails.nodePrefix;
      createResumeKubernetesUniverseTasks();

      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.ResumeUniverse.name(),
          getUserTaskUUID(),
          UniverseState.READY,
          null);
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.ResumeUniverse.name(),
          getUserTaskUUID(),
          UniverseState.ERROR_RESUMING,
          t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
