// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UnregisterUniverseFromPaCollector;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UnregisterUniverseFromPACollector extends UniverseTaskBase {

  @Inject
  protected UnregisterUniverseFromPACollector(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUuid;
    public UUID paCollectorUuid;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Started {} task.", getName());
    try {
      lockAndFreezeUniverseForUpdate(-1, null);

      SubTaskGroup subTaskGroup =
          createSubTaskGroup(
              "UnregisterUniverseFromPaCollector", SubTaskGroupType.ConfigureUniverse);
      UnregisterUniverseFromPaCollector.Params subtaskParams =
          new UnregisterUniverseFromPaCollector.Params();
      subtaskParams.setUniverseUUID(taskParams().getUniverseUUID());
      UnregisterUniverseFromPaCollector task = createTask(UnregisterUniverseFromPaCollector.class);
      task.initialize(subtaskParams);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
      getRunnableTask().addSubTaskGroup(subTaskGroup);

      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
