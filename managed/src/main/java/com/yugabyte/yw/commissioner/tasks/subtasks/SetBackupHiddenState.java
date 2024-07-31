// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Backup;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetBackupHiddenState extends AbstractTaskBase {

  @Inject
  public SetBackupHiddenState(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID backupUUID;
    public boolean hidden;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(backupUUID=%s,customerUUID=%s,hidden=%b)",
        super.getName(), taskParams().backupUUID, taskParams().customerUUID, taskParams().hidden);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Backup backup = Backup.getOrBadRequest(taskParams().customerUUID, taskParams().backupUUID);

    backup.setHidden(taskParams().hidden);
    backup.update();

    log.info("Completed {}", getName());
  }
}
