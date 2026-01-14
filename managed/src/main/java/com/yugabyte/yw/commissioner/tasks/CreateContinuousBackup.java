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

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.prometheus.metrics.core.metrics.Gauge;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.io.File;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class CreateContinuousBackup extends AbstractTaskBase {

  private final StorageUtilFactory storageUtilFactory;
  private final PlatformReplicationHelper replicationHelper;
  private final PlatformReplicationManager replicationManager;

  private static Gauge CONT_BACKUP_FAILING_GAUGE =
      Gauge.builder()
          .name("yba_cont_backup_status")
          .help("Continuous backups failing")
          .labelNames("storage_loc")
          .register(PrometheusRegistry.defaultRegistry);

  public static void clearGauge() {
    CONT_BACKUP_FAILING_GAUGE.clear();
  }

  @Inject
  protected CreateContinuousBackup(
      BaseTaskDependencies baseTaskDependencies,
      PlatformReplicationHelper replicationHelper,
      PlatformReplicationManager replicationManager,
      StorageUtilFactory storageUtilFactory) {
    super(baseTaskDependencies);
    this.replicationHelper = replicationHelper;
    this.replicationManager = replicationManager;
    this.storageUtilFactory = storageUtilFactory;
  }

  public static class Params extends AbstractTaskParams {
    public ContinuousBackupConfig cbConfig;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public void runScheduledBackup(
      Schedule schedule, Commissioner commissioner, boolean alreadyRunning) {
    log.info("Execution of scheduled YBA backup");
    if (alreadyRunning) {
      log.info("Continuous backup already running, skipping.");
      return;
    }
    UUID customerUUID = schedule.getCustomerUUID();
    Customer customer = Customer.get(customerUUID);
    CreateContinuousBackup.Params taskParams =
        Json.fromJson(schedule.getTaskParams(), CreateContinuousBackup.Params.class);

    if (schedule.isBacklogStatus()) {
      schedule.updateBacklogStatus(false);
    }

    UUID taskUUID = commissioner.submit(TaskType.CreateContinuousBackup, taskParams);
    ScheduleTask.create(taskUUID, schedule.getScheduleUUID());
    CustomerTask.create(
        customer,
        Util.NULL_UUID,
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.CreateYbaBackup,
        Util.getYwHostnameOrIP());
    log.info("Submitted continuous yba backup creation with task uuid = {}.", taskUUID);
  }

  @Override
  public void run() {
    log.info("Execution of CreateContinuousBackup");
    CreateContinuousBackup.Params taskParams = taskParams();
    ContinuousBackupConfig cbConfig = taskParams.cbConfig;
    UUID storageConfigUUID = cbConfig.getStorageConfigUUID();
    String dirName = cbConfig.getBackupDir();

    if (storageConfigUUID == null) {
      log.info("No storage config UUID set, skipping creation of YBA backup.");
      return;
    }
    log.debug("Creating platform backup...");
    ShellResponse response =
        replicationHelper.runCommand(replicationManager.new CreatePlatformBackupParams());

    if (response.code != 0) {
      log.error("Backup failed: " + response.message);
      CONT_BACKUP_FAILING_GAUGE.labelValues(cbConfig.getStorageLocation()).set(0);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Backup failed: " + response.message);
    }
    Optional<File> backupOpt = replicationHelper.getMostRecentBackup();
    if (!backupOpt.isPresent()) {
      CONT_BACKUP_FAILING_GAUGE.labelValues(cbConfig.getStorageLocation()).set(0);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "could not find backup file");
    }
    File backup = backupOpt.get();
    CustomerConfig customerConfig = CustomerConfig.get(storageConfigUUID);
    if (customerConfig == null) {
      CONT_BACKUP_FAILING_GAUGE.labelValues(cbConfig.getStorageLocation()).set(0);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Could not find customer config with provided storage config UUID during create.");
    }
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(customerConfig.getName());
    if (!storageUtil.uploadYbaBackup(customerConfig.getDataObject(), backup, dirName)) {
      CONT_BACKUP_FAILING_GAUGE.labelValues(cbConfig.getStorageLocation()).set(0);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not upload YBA backup to cloud storage.");
    }

    // Mark success as rest of work is best effort anyways
    cbConfig.updateLastBackup();
    CONT_BACKUP_FAILING_GAUGE.labelValues(cbConfig.getStorageLocation()).set(1);

    if (!storageUtil.cleanupUploadedBackups(customerConfig.getDataObject(), dirName)) {
      log.warn(
          "Error cleaning up uploaded backups to cloud storage, please delete manually to avoid"
              + " incurring unexpected costs.");
    }

    Set<String> remoteReleases =
        storageUtil.getRemoteReleaseVersions(customerConfig.getDataObject(), dirName);
    // Get all local full paths
    Map<String, ReleaseContainer> localReleaseContainers =
        releaseManager.getAllLocalReleaseContainersByVersion();
    // Remove all that match a remote filename
    Set<String> toUploadReleases = localReleaseContainers.keySet();
    toUploadReleases.removeAll(remoteReleases);
    toUploadReleases.forEach(
        releaseVer -> {
          ReleaseContainer container = localReleaseContainers.get(releaseVer);
          container
              .getLocalReleasePathStrings()
              .forEach(
                  release -> {
                    storageUtil.uploadYBDBRelease(
                        customerConfig.getDataObject(),
                        new File(release),
                        dirName,
                        container.getVersion());
                  });
        });
    // Cleanup backups
    replicationHelper.cleanupCreatedBackups();
    return;
  }
}
