// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Restore;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetRestoreState extends AbstractTaskBase {

  @Inject
  public SetRestoreState(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID restoreUUID;
    public Restore.State state;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(restoreUUID=%s,state=%s)",
        super.getName(), taskParams().restoreUUID, taskParams().state);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Optional<Restore> restoreOptional = Restore.fetchRestore(taskParams().restoreUUID);
    if (restoreOptional.isPresent()) {
      Restore restore = restoreOptional.get();
      restore.update(restore.getTaskUUID(), taskParams().state);
    }

    log.info("Completed {}", getName());
  }
}
