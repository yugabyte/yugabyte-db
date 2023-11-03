package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunExternalScript;

public class ExternalScript extends AbstractTaskBase {
  @Inject
  protected ExternalScript(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public RunExternalScript.Params params() {
    return (RunExternalScript.Params) taskParams;
  }

  @Override
  public void run() {
    RunExternalScript task = null;
    try {
      SubTaskGroup subTaskGroup = createSubTaskGroup("RunExternalScript");
      task = createTask(RunExternalScript.class);
      task.initialize(params());
      subTaskGroup.addSubTask(task);
      getRunnableTask().addSubTaskGroup(subTaskGroup);
      getRunnableTask().runSubTasks();
    } catch (Exception e) {
      throw new RuntimeException(e);
    } finally {
      if (task != null) {
        task.terminate();
      }
    }
  }
}
