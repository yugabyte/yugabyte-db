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
public class SetRestoreHiddenState extends AbstractTaskBase {

  @Inject
  public SetRestoreHiddenState(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID restoreUUID;
    public boolean hidden;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(restoreUUID=%s,hidden=%b)",
        super.getName(), taskParams().restoreUUID, taskParams().hidden);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Optional<Restore> restoreOptional = Restore.fetchRestore(taskParams().restoreUUID);
    if (!restoreOptional.isPresent()) {
      log.debug(
          "Restore with uuid: {} not found, skipping setting hidden state to: {}",
          taskParams().restoreUUID,
          taskParams().hidden);

      throw new RuntimeException(
          String.format("Restore with uuid: %s not found", taskParams().restoreUUID));
    }

    Restore restore = restoreOptional.get();
    restore.setHidden(taskParams().hidden);
    restore.update();

    log.info("Completed {}", getName());
  }
}
