/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.amazonaws.SDKGlobalConfiguration;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.CloudUtilFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteCustomerConfig extends UniverseTaskBase {

  private static final String AZ = Util.AZ;
  private static final String GCS = Util.GCS;
  private static final String S3 = Util.S3;
  private static final String NFS = Util.NFS;

  private final BackupHelper backupHelper;
  private final CloudUtilFactory cloudUtilFactory;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public DeleteCustomerConfig(
      BaseTaskDependencies baseTaskDependencies,
      BackupHelper backupHelper,
      CloudUtilFactory cloudUtilFactory,
      RuntimeConfGetter runtimeConfGetter) {
    super(baseTaskDependencies);
    this.backupHelper = backupHelper;
    this.cloudUtilFactory = cloudUtilFactory;
    this.runtimeConfGetter = runtimeConfGetter;
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
    public UUID configUUID;
    public Boolean isDeleteBackups;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName() + "(" + params().customerUUID + ")";
  }

  @Override
  public void run() {
    try {
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
        System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");
      }
      List<Schedule> scheduleList = Schedule.findAllScheduleWithCustomerConfig(params().configUUID);
      for (Schedule schedule : scheduleList) {
        schedule.stopSchedule();
      }
      CustomerConfig customerConfig =
          CustomerConfig.get(params().customerUUID, params().configUUID);
      List<Backup> backupList =
          Backup.findAllFinishedBackupsWithCustomerConfig(params().configUUID);

      if (backupList.size() != 0) {
        if (isCredentialUsable(customerConfig)) {
          List<String> backupLocations;
          switch (customerConfig.getName()) {
            case S3:
            case GCS:
            case AZ:
              for (Backup backup : backupList) {
                try {
                  CloudUtil cloudUtil = cloudUtilFactory.getCloudUtil(customerConfig.getName());
                  backupLocations = BackupUtil.getBackupLocations(backup);
                  cloudUtil.deleteKeyIfExists(
                      customerConfig.getDataObject(), backupLocations.get(0));
                  cloudUtil.deleteStorage(customerConfig.getDataObject(), backupLocations);
                } catch (Exception e) {
                  log.error(" Error in deleting backup " + backup.getBackupUUID().toString(), e);
                  backup.transitionState(Backup.BackupState.FailedToDelete);
                } finally {
                  if (backup.getState() != Backup.BackupState.FailedToDelete) {
                    backup.delete();
                  }
                }
              }
              break;
            case NFS:
              List<Backup> nfsBackupList =
                  backupList.parallelStream()
                      .filter(backup -> isUniversePresent(backup))
                      .collect(Collectors.toList());
              backupList.parallelStream()
                  .filter(backup -> !isUniversePresent(backup))
                  .forEach(backup -> backup.transitionState(Backup.BackupState.FailedToDelete));
              if (!nfsBackupList.isEmpty()) {
                createDeleteBackupTasks(nfsBackupList, params().customerUUID);
              }
              break;
            default:
              log.error("Invalid Config type {} provided", customerConfig.getName());
          }
        } else {
          backupList.parallelStream()
              .forEach(backup -> backup.transitionState(Backup.BackupState.FailedToDelete));
        }
      }

      getRunnableTask().runSubTasks();
    } catch (Exception e) {
      log.error(
          "Error while deleting backups associated to Configuration {}", params().configUUID, e);
    } finally {
      CustomerConfig customerConfig =
          CustomerConfig.get(params().customerUUID, params().configUUID);
      customerConfig.delete();
      // Re-enable cert checking as it applies globally
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
        System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
      }
    }
    log.info("Finished {} task.", getName());
  }

  private Boolean isUniversePresent(Backup backup) {
    Optional<Universe> universe = Universe.maybeGet(backup.getBackupInfo().getUniverseUUID());
    return universe.isPresent();
  }

  private Boolean isCredentialUsable(CustomerConfig config) {
    Boolean isValid = true;
    try {
      backupHelper.validateStorageConfig(config);
    } catch (PlatformServiceException e) {
      isValid = false;
    }
    return isValid;
  }
}
