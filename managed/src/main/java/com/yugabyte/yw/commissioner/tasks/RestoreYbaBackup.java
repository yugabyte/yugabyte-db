/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import java.io.File;
import java.nio.file.Paths;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestoreYbaBackup extends AbstractTaskBase {

  private final PlatformReplicationManager replicationManager;

  @Inject
  protected RestoreYbaBackup(
      BaseTaskDependencies baseTaskDependencies, PlatformReplicationManager replicationManager) {
    super(baseTaskDependencies);
    this.replicationManager = replicationManager;
  }

  public static class Params extends AbstractTaskParams {
    public String localPath;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    TaskInfo taskInfo = getRunnableTask().getTaskInfo();
    UUID taskInfoUUID = taskInfo.getUuid();
    CustomerTask customerTask = CustomerTask.findByTaskUUID(taskInfoUUID);
    RestoreYbaBackup.Params taskParams = taskParams();
    File backupFile = Paths.get(taskParams.localPath).toFile();
    if (!backupFile.exists()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "could not find backup file " + taskParams.localPath);
    }
    if (!replicationManager.restoreBackup(backupFile)) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "YBA restore failed.");
    }

    // Successful restore so write task info to file
    Util.writeRestoreTaskInfo(customerTask, taskInfo);

    Util.shutdownYbaProcess(0);
    return;
  }
}
