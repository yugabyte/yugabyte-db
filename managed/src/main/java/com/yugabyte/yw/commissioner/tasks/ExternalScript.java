package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
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
    createThreadpool();
    try {
      SubTaskGroupQueue subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      RunExternalScript task = createTask(RunExternalScript.class);
      task.initialize(params());
      SubTaskGroup subTaskGroup = new SubTaskGroup("RunExternalScript", executor);
      subTaskGroup.addTask(task);
      subTaskGroupQueue.add(subTaskGroup);
      subTaskGroupQueue.run();
    } catch (Exception e) {
      throw new RuntimeException(e);
    } finally {
      executor.shutdownNow();
    }
  }
}
