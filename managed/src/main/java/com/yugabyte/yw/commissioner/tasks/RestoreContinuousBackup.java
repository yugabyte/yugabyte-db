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
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.CloudUtilFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.io.File;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestoreContinuousBackup extends AbstractTaskBase {

  private final CloudUtilFactory cloudUtilFactory;
  private final PlatformReplicationHelper replicationHelper;
  private final PlatformReplicationManager replicationManager;
  private final Config appConfig;
  private final ReleaseManager releaseManager;

  @Inject
  protected RestoreContinuousBackup(
      BaseTaskDependencies baseTaskDependencies,
      PlatformReplicationHelper replicationHelper,
      PlatformReplicationManager replicationManager,
      CloudUtilFactory cloudUtilFactory,
      Config appConfig,
      ReleaseManager releaseManager) {
    super(baseTaskDependencies);
    this.replicationHelper = replicationHelper;
    this.replicationManager = replicationManager;
    this.cloudUtilFactory = cloudUtilFactory;
    this.appConfig = appConfig;
    this.releaseManager = releaseManager;
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
    CloudUtil cloudUtil = cloudUtilFactory.getCloudUtil(customerConfig.getName());
    File backup =
        cloudUtil.downloadYbaBackup(
            customerConfig.getDataObject(),
            taskParams.backupDir,
            replicationHelper.getReplicationDirFor(taskParams.storageConfigUUID.toString()));
    if (backup == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not download YBA backup from cloud storage.");
    }

    // Restore any missing YBDB releases from remote storage
    Set<String> toDownloadReleases =
        cloudUtil.getRemoteReleaseVersions(customerConfig.getDataObject(), taskParams.backupDir);
    Map<String, ReleaseContainer> localReleaseContainers =
        releaseManager.getAllLocalReleaseContainersByVersion();
    toDownloadReleases.removeAll(localReleaseContainers.keySet());
    if (!cloudUtil.downloadRemoteReleases(
        customerConfig.getDataObject(),
        toDownloadReleases,
        appConfig.getString(Util.YB_RELEASES_PATH),
        taskParams.backupDir)) {
      log.warn(
          "Error downloading YBDB releases from remote storage location, some universe operations"
              + " may not be permitted.");
    }

    if (!replicationManager.restoreBackup(backup)) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error restoring backup to YBA");
    }
    // Restart YBA to cause changes to take effect
    // Do we want to manually insert RestoreContinuousBackup task info?
    Util.shutdownYbaProcess(0);
    return;
  }
}
