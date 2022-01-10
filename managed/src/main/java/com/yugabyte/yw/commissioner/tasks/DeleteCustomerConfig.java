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
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
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

  @Inject
  public DeleteCustomerConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
      // Disable cert checking while connecting with s3
      // Enabling it can potentially fail when s3 compatible storages like
      // Dell ECS are provided and custom certs are needed to connect
      // Reference: https://yugabyte.atlassian.net/browse/PLAT-2497
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      List<Schedule> scheduleList = Schedule.findAllScheduleWithCustomerConfig(params().configUUID);
      for (Schedule schedule : scheduleList) {
        schedule.stopSchedule();
      }
      CustomerConfig customerConfig =
          CustomerConfig.get(params().customerUUID, params().configUUID);
      List<Backup> backupList =
          Backup.findAllFinishedBackupsWithCustomerConfig(params().configUUID);

      if (backupList.size() != 0) {
        if (isCredentialUsable(customerConfig.data, customerConfig.name)) {
          List<String> backupLocations;
          switch (customerConfig.name) {
            case S3:
              for (Backup backup : backupList) {
                try {
                  backupLocations = getBackupLocations(backup);
                  AWSUtil.deleteKeyIfExists(customerConfig.data, backupLocations.get(0));
                  AWSUtil.deleteStorage(customerConfig.data, backupLocations);
                } catch (Exception e) {
                  log.error(" Error in deleting backup " + backup.backupUUID.toString(), e);
                  backup.transitionState(Backup.BackupState.FailedToDelete);
                } finally {
                  if (backup.state != Backup.BackupState.FailedToDelete) {
                    backup.transitionState(Backup.BackupState.Deleted);
                  }
                }
              }
              break;
            case GCS:
              for (Backup backup : backupList) {
                try {
                  backupLocations = getBackupLocations(backup);
                  GCPUtil.deleteKeyIfExists(customerConfig.data, backupLocations.get(0));
                  GCPUtil.deleteStorage(customerConfig.data, backupLocations);
                } catch (Exception e) {
                  log.error(" Error in deleting backup " + backup.backupUUID.toString(), e);
                  backup.transitionState(Backup.BackupState.FailedToDelete);
                } finally {
                  if (backup.state != Backup.BackupState.FailedToDelete) {
                    backup.transitionState(Backup.BackupState.Deleted);
                  }
                }
              }
              break;
            case AZ:
              for (Backup backup : backupList) {
                try {
                  backupLocations = getBackupLocations(backup);
                  AZUtil.deleteKeyIfExists(customerConfig.data, backupLocations.get(0));
                  AZUtil.deleteStorage(customerConfig.data, backupLocations);
                } catch (Exception e) {
                  log.error(" Error in deleting backup " + backup.backupUUID.toString(), e);
                  backup.transitionState(Backup.BackupState.FailedToDelete);
                } finally {
                  if (backup.state != Backup.BackupState.FailedToDelete) {
                    backup.transitionState(Backup.BackupState.Deleted);
                  }
                }
              }
              break;
            case NFS:
              List<Backup> nfsBackupList =
                  backupList
                      .parallelStream()
                      .filter(backup -> isUniversePresent(backup))
                      .collect(Collectors.toList());
              backupList
                  .parallelStream()
                  .filter(backup -> !isUniversePresent(backup))
                  .forEach(backup -> backup.transitionState(Backup.BackupState.FailedToDelete));
              if (!nfsBackupList.isEmpty()) {
                createDeleteBackupTasks(nfsBackupList, params().customerUUID);
              }
              break;
            default:
              log.error("Invalid Config type {} provided", customerConfig.name);
          }
        } else {
          backupList
              .parallelStream()
              .forEach(backup -> backup.transitionState(Backup.BackupState.FailedToDelete));
        }
      }

      subTaskGroupQueue.run();
    } catch (Exception e) {
      log.error(
          "Error while deleting backups associated to Configuration {}", params().configUUID, e);
    } finally {
      CustomerConfig customerConfig =
          CustomerConfig.get(params().customerUUID, params().configUUID);
      customerConfig.delete();
      // Re-enable cert checking as it applies globally
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
    }
    log.info("Finished {} task.", getName());
  }

  private Boolean isUniversePresent(Backup backup) {
    Optional<Universe> universe = Universe.maybeGet(backup.getBackupInfo().universeUUID);
    return universe.isPresent();
  }

  private static List<String> getBackupLocations(Backup backup) {
    BackupTableParams backupParams = backup.getBackupInfo();
    List<String> backupLocations = new ArrayList<>();
    if (backupParams.backupList != null) {
      for (BackupTableParams params : backupParams.backupList) {
        backupLocations.add(params.storageLocation);
      }
    } else {
      backupLocations.add(backupParams.storageLocation);
    }
    return backupLocations;
  }

  private Boolean isCredentialUsable(JsonNode credentials, String configName) {
    Boolean isValid;
    switch (configName) {
      case S3:
        isValid = AWSUtil.canCredentialListObjects(credentials);
        break;
      case GCS:
        isValid = GCPUtil.canCredentialListObjects(credentials);
        break;
      case AZ:
        isValid = AZUtil.canCredentialListObjects(credentials);
        break;
      case NFS:
        isValid = true;
        break;
      default:
        log.error("Invalid Config type {} provided", configName);
        isValid = false;
    }
    return isValid;
  }
}
