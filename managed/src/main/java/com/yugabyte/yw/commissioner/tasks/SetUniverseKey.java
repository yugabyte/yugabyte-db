/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetUniverseKey extends UniverseTaskBase {

  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected SetUniverseKey(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  // Only set when the Kubernetes operator submitted this task. On the API path there is no CR to
  // report against, so the status calls are skipped entirely.
  private boolean isOperatorTask() {
    return taskParams().getKubernetesResourceDetails() != null;
  }

  @Override
  protected EncryptionAtRestKeyParams taskParams() {
    return (EncryptionAtRestKeyParams) taskParams;
  }

  @Override
  public void run() {
    log.info("Started {} task.", getName());
    Throwable th = null;
    try {
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);
      preTaskActions();

      if (isOperatorTask()) {
        kubernetesStatus.startYBUniverseEventStatus(
            universe,
            taskParams().getKubernetesResourceDetails(),
            TaskType.SetUniverseKey.name(),
            getUserTaskUUID(),
            UniverseState.EDITING);
      }

      // Manage encryption at rest
      log.debug(
          "Current EAR status is {} for universe {} in the YBA Universe details.",
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .encryptionAtRestConfig
              .opType
              .name(),
          taskParams().getUniverseUUID());
      log.info(
          "Setting EAR status {} for universe {} in the DB nodes.",
          taskParams().encryptionAtRestConfig.opType.name(),
          taskParams().getUniverseUUID());
      SubTaskGroup manageEncryptionKeyTask = createManageEncryptionAtRestTask();
      if (manageEncryptionKeyTask != null) {
        manageEncryptionKeyTask.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
      log.info(
          "Successfully set EAR status {} for universe {} in the DB nodes.",
          taskParams().encryptionAtRestConfig.opType.name(),
          taskParams().getUniverseUUID());
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      th = t;
      throw t;
    } finally {
      if (isOperatorTask()) {
        kubernetesStatus.updateYBUniverseStatus(
            getUniverse(),
            taskParams().getKubernetesResourceDetails(),
            TaskType.SetUniverseKey.name(),
            getUserTaskUUID(),
            (th != null) ? UniverseState.ERROR_UPDATING : UniverseState.READY,
            th);
      }
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
