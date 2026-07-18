// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.YbcThrottleTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class UpdateYbcThrottleFlags extends UniverseTaskBase {
  private final YbcManager ybcManager;

  @Override
  protected YbcThrottleTaskParams taskParams() {
    return (YbcThrottleTaskParams) taskParams;
  }

  @Inject
  protected UpdateYbcThrottleFlags(
      BaseTaskDependencies baseTaskDependencies, YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcManager = ybcManager;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    String errorString = null;
    try {
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);
      createSetYbcThrottleParamsSubTasks(
          universe, taskParams().getYbcThrottleParameters(), ybcManager);
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
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
