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
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.io.File;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class RestoreContinuousBackup extends AbstractTaskBase {

  private final StorageUtilFactory storageUtilFactory;
  private final PlatformReplicationHelper replicationHelper;
  private final PlatformReplicationManager replicationManager;
  private final Config appConfig;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  protected RestoreContinuousBackup(
      BaseTaskDependencies baseTaskDependencies,
      PlatformReplicationHelper replicationHelper,
      PlatformReplicationManager replicationManager,
      StorageUtilFactory storageUtilFactory,
      Config appConfig,
      RuntimeConfGetter runtimeConfGetter) {
    super(baseTaskDependencies);
    this.replicationHelper = replicationHelper;
    this.replicationManager = replicationManager;
    this.storageUtilFactory = storageUtilFactory;
    this.appConfig = appConfig;
    this.runtimeConfGetter = runtimeConfGetter;
  }

  public static class Params extends AbstractTaskParams {
    public UUID storageConfigUUID;
    public String backupDir;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Exeuction of RestoreContinuousBackup");
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
    RestoreContinuousBackup.Params taskParams = taskParams();
    if (taskParams.storageConfigUUID == null) {
      log.info("No storage config UUID set, skipping restore.");
    }
    // Download backup from remote location
    CustomerConfig customerConfig = CustomerConfig.get(taskParams.storageConfigUUID);
    if (customerConfig == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Could not find customer config with provided storage config UUID during restore.");
    }
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(customerConfig.getName());
    File backup =
        storageUtil.downloadYbaBackup(
            customerConfig.getDataObject(),
            taskParams.backupDir,
            replicationHelper.getReplicationDirFor(taskParams.storageConfigUUID.toString()));
    if (backup == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not download YBA backup from cloud storage.");
    }

    // Restore any missing YBDB releases from remote storage
    Set<String> toDownloadReleases =
        storageUtil.getRemoteReleaseVersions(customerConfig.getDataObject(), taskParams.backupDir);
    Map<String, ReleaseContainer> localReleaseContainers =
        releaseManager.getAllLocalReleaseContainersByVersion();
    toDownloadReleases.removeAll(localReleaseContainers.keySet());
    if (!storageUtil.downloadRemoteReleases(
        customerConfig.getDataObject(),
        toDownloadReleases,
        appConfig.getString(Util.YB_RELEASES_PATH),
        taskParams.backupDir)) {
      log.warn(
          "Error downloading YBDB releases from remote storage location, some universe operations"
              + " may not be permitted.");
    }

    if (!replicationManager.restoreBackup(backup, true /* k8sRestoreYbaDbOnRestart */)) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error restoring backup to YBA");
    }

    // Successful restore so write task info to file
    Util.writeRestoreTaskInfo(customerTask, taskInfo);

    // Restart YBA to cause changes to take effect
    // Do we want to manually insert RestoreContinuousBackup task info?
    Util.shutdownYbaProcess(0);
    return;
  }
}
