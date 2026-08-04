/*
 * Copyright YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import api.v2.models.YbaComponent;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.AbstractTaskParams;
import java.io.File;
import java.nio.file.Paths;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateYbaBackup extends AbstractTaskBase {

  private final PlatformReplicationHelper replicationHelper;
  private final PlatformReplicationManager replicationManager;

  @Inject
  protected CreateYbaBackup(
      BaseTaskDependencies baseTaskDependencies,
      PlatformReplicationHelper replicationHelper,
      PlatformReplicationManager replicationManager) {
    super(baseTaskDependencies);
    this.replicationHelper = replicationHelper;
    this.replicationManager = replicationManager;
  }

  public static class Params extends AbstractTaskParams {
    public String localPath;
    public List<YbaComponent> components;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    CreateYbaBackup.Params taskParams = taskParams();
    if (taskParams.localPath == null || taskParams.localPath.isBlank()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Backup directory path must be set.");
    }
    File backupDir = Paths.get(taskParams.localPath).toFile();
    if (!backupDir.exists() && !backupDir.mkdirs()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Backup directory could not be created.");
    }
    boolean excludeReleases = !taskParams.components.contains(YbaComponent.RELEASES);
    boolean excludePrometheus = !taskParams.components.contains(YbaComponent.PROMETHEUS);
    ShellResponse response =
        replicationHelper.runCommand(
            replicationManager
            .new CreatePlatformBackupParams(
                excludePrometheus, excludeReleases, taskParams.localPath));
    if (response.code != 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Backup failed: " + response.message);
    }

    return;
  }
}
