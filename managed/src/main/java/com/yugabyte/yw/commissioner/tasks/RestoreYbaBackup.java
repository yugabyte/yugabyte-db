/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.nio.file.Paths;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class RestoreYbaBackup extends AbstractTaskBase {

  private final PlatformReplicationManager replicationManager;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  protected RestoreYbaBackup(
      BaseTaskDependencies baseTaskDependencies,
      PlatformReplicationManager replicationManager,
      RuntimeConfGetter runtimeConfGetter) {
    super(baseTaskDependencies);
    this.replicationManager = replicationManager;
    this.runtimeConfGetter = runtimeConfGetter;
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
    Set<UUID> universeUUIDs = Universe.getAllUUIDs();
    if (CollectionUtils.isNotEmpty(universeUUIDs)) {
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.allowYbaRestoreWithUniverses)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "YBA restore is not allowed when existing universes are present, please delete before"
                + " restoring");
      }
    }
    TaskInfo taskInfo = getRunnableTask().getTaskInfo();
    UUID taskInfoUUID = taskInfo.getUuid();
    CustomerTask customerTask = CustomerTask.findByTaskUUID(taskInfoUUID);
    RestoreYbaBackup.Params taskParams = taskParams();
    File backupFile = Paths.get(taskParams.localPath).toFile();
    if (!backupFile.exists()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "could not find backup file " + taskParams.localPath);
    }
    // Check if backup file is less than 1 day old
    if (backupFile.lastModified() < System.currentTimeMillis() - (1000 * 60 * 60 * 24)) {
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.allowYbaRestoreWithOldBackup)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "YBA restore is not allowed when backup file is more than 1 day old");
      }
    }
    if (!replicationManager.restoreBackup(
        backupFile,
        true /* k8sRestoreYbaDbOnRestart */,
        KubernetesEnvironmentVariables.isYbaRunningInKubernetes() /* skipOldFiles */)) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "YBA restore failed.");
    }

    // Successful restore so write task info to file
    Util.writeRestoreTaskInfo(customerTask, taskInfo);

    Util.shutdownYbaProcess(0);
    return;
  }
}
