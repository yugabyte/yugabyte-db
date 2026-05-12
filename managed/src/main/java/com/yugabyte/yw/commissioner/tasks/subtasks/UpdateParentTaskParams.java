// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import java.util.UUID;
import java.util.function.Consumer;
import javax.inject.Inject;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

/** Subtask to save index to parent task's taskParams for retryability. */
@Slf4j
public class UpdateParentTaskParams extends AbstractTaskBase {

  @Inject
  protected UpdateParentTaskParams(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    @Setter @Getter private Consumer<TaskInfo> taskInfoConsumer;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID userTaskUUID = getUserTaskUUID();
    if (userTaskUUID == null) {
      throw new RuntimeException("User task UUID is null, cannot save task params");
    }
    log.info("Running {} to update taskParams for User Task {}", getName(), userTaskUUID);
    TaskInfo.updateInTxn(
        userTaskUUID,
        tf -> {
          taskParams().taskInfoConsumer.accept(tf);
        });
    log.info("Completed {}", getName());
  }
}
