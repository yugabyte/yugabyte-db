// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.PushPaExportConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.RegisterUniverseWithPaCollector;
import com.yugabyte.yw.common.pa.PaRegistrationMode;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RegisterUniverseWithPACollector extends UniverseTaskBase {

  @Inject
  protected RegisterUniverseWithPACollector(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUuid;
    public UUID paCollectorUuid;
    public PaRegistrationMode mode;

    /** The destination, for ONLINE only. */
    public UUID paEndpointUuid;
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

      // The destination has to exist on the collector before a universe can name it, so this runs
      // as its own group ahead of the registration rather than inside it.
      if (taskParams().mode != null && taskParams().mode.requiresExportConfig()) {
        SubTaskGroup pushGroup =
            createSubTaskGroup("PushPaExportConfig", SubTaskGroupType.ConfigureUniverse);
        PushPaExportConfig.Params pushParams = new PushPaExportConfig.Params();
        pushParams.setUniverseUUID(taskParams().getUniverseUUID());
        pushParams.paCollectorUuid = taskParams().paCollectorUuid;
        pushParams.paEndpointUuid = taskParams().paEndpointUuid;
        PushPaExportConfig pushTask = createTask(PushPaExportConfig.class);
        pushTask.initialize(pushParams);
        pushTask.setUserTaskUUID(getUserTaskUUID());
        pushGroup.addSubTask(pushTask);
        getRunnableTask().addSubTaskGroup(pushGroup);
      }

      SubTaskGroup subTaskGroup =
          createSubTaskGroup("RegisterUniverseWithPaCollector", SubTaskGroupType.ConfigureUniverse);
      RegisterUniverseWithPaCollector.Params subtaskParams =
          new RegisterUniverseWithPaCollector.Params();
      subtaskParams.setUniverseUUID(taskParams().getUniverseUUID());
      subtaskParams.paCollectorUuid = taskParams().paCollectorUuid;
      subtaskParams.mode = taskParams().mode;
      subtaskParams.paEndpointUuid = taskParams().paEndpointUuid;
      RegisterUniverseWithPaCollector task = createTask(RegisterUniverseWithPaCollector.class);
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
