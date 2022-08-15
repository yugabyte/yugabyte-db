// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.amazonaws.SDKGlobalConfiguration;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class BackupGarbageCollector {

  private final PlatformScheduler platformScheduler;

  private final TableManagerYb tableManagerYb;

  private final YbcManager ybcManager;

  private final CustomerConfigService customerConfigService;

  private final BackupUtil backupUtil;

  private final RuntimeConfigFactory runtimeConfigFactory;

  private static final String YB_BACKUP_GARBAGE_COLLECTOR_INTERVAL = "yb.backupGC.gc_run_interval";

  private static final String AZ = Util.AZ;
  private static final String GCS = Util.GCS;
  private static final String S3 = Util.S3;
  private static final String NFS = Util.NFS;

  @Inject
  public BackupGarbageCollector(
      PlatformScheduler platformScheduler,
      CustomerConfigService customerConfigService,
      RuntimeConfigFactory runtimeConfigFactory,
      TableManagerYb tableManagerYb,
      BackupUtil backupUtil,
      YbcManager ybcManager) {
    this.platformScheduler = platformScheduler;
    this.customerConfigService = customerConfigService;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.tableManagerYb = tableManagerYb;
    this.backupUtil = backupUtil;
    this.ybcManager = ybcManager;
  }

  public void start() {
    Duration gcInterval = this.gcRunInterval();
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, gcInterval, this::scheduleRunner);
  }

  private Duration gcRunInterval() {
    return runtimeConfigFactory
        .staticApplicationConf()
        .getDuration(YB_BACKUP_GARBAGE_COLLECTOR_INTERVAL);
  }

  void scheduleRunner() {
    log.info("Running Backup Garbage Collector");
    try {
      List<Customer> customersList = Customer.getAll();

      // Delete the backups associated with customer storage config which are in QueuedForDeletion
      // state.
      // After Deleting all associated backups we can delete the storage config.
      customersList.forEach(
          (customer) -> {
            List<CustomerConfig> configList =
                CustomerConfig.getAllStorageConfigsQueuedForDeletion(customer.uuid);
            configList.forEach(
                (config) -> {
                  try {
                    List<Backup> backupList =
                        Backup.findAllBackupsQueuedForDeletionWithCustomerConfig(
                            config.configUUID, customer.uuid);
                    backupList.forEach(backup -> deleteBackup(customer.uuid, backup.backupUUID));
                  } catch (Exception e) {
                    log.error(
                        "Error occured while deleting backups associated with {} storage config",
                        config.configName);
                  } finally {
                    config.delete();
                    log.info("Customer Storage config {} is deleted", config.configName);
                  }
                });
          });

      customersList.forEach(
          (customer) -> {
            List<Backup> backupList = Backup.findAllBackupsQueuedForDeletion(customer.uuid);
            if (backupList != null) {
              backupList.forEach((backup) -> deleteBackup(customer.uuid, backup.backupUUID));
            }
          });
    } catch (Exception e) {
      log.error("Error running backup garbage collector", e);
    }
  }

  public synchronized void deleteBackup(UUID customerUUID, UUID backupUUID) {
    Backup backup = Backup.maybeGet(customerUUID, backupUUID).orElse(null);
    // Backup is already deleted.
    if (backup == null || backup.state == BackupState.Deleted) {
      if (backup != null) {
        backup.delete();
      }
      return;
    }
    try {
      // Disable cert checking while connecting with s3
      // Enabling it can potentially fail when s3 compatible storages like
      // Dell ECS are provided and custom certs are needed to connect
      // Reference: https://yugabyte.atlassian.net/browse/PLAT-2497
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");

      UUID storageConfigUUID = backup.getBackupInfo().storageConfigUUID;
      CustomerConfig customerConfig =
          customerConfigService.getOrBadRequest(backup.customerUUID, storageConfigUUID);
      if (isCredentialUsable(customerConfig)) {
        List<String> backupLocations = null;
        log.info("Backup {} deletion started", backupUUID);
        backup.transitionState(BackupState.DeleteInProgress);
        try {
          switch (customerConfig.name) {
              // for cases S3, NFS, GCS, we get Util from CloudUtil class
            case S3:
              CustomerConfigStorageS3Data s3Data =
                  (CustomerConfigStorageS3Data) customerConfig.getDataObject();
              if (s3Data.isIAMInstanceProfile) {
                if (isUniversePresent(backup)) {
                  BackupTableParams backupParams = backup.getBackupInfo();
                  List<BackupTableParams> backupList =
                      backupParams.backupList == null
                          ? ImmutableList.of(backupParams)
                          : backupParams.backupList;
                  boolean success = deleteScriptBackup(backupList);
                  if (success) {
                    backup.delete();
                    log.info("Backup {} is successfully deleted", backupUUID);
                  } else {
                    backup.transitionState(BackupState.FailedToDelete);
                  }
                } else {
                  backup.transitionState(BackupState.FailedToDelete);
                  log.info(
                      "Cannot delete S3 IAM Backup {} as universe is not present",
                      backup.backupUUID);
                }
                break;
              }
            case GCS:
            case AZ:
              CloudUtil cloudUtil = CloudUtil.getCloudUtil(customerConfig.name);
              backupLocations = backupUtil.getBackupLocations(backup);
              cloudUtil.deleteKeyIfExists(customerConfig.getDataObject(), backupLocations.get(0));
              cloudUtil.deleteStorage(customerConfig.getDataObject(), backupLocations);
              backup.delete();
              log.info("Backup {} is successfully deleted", backupUUID);
              break;
            case NFS:
              if (isUniversePresent(backup)) {
                BackupTableParams backupParams = backup.getBackupInfo();
                List<BackupTableParams> backupList =
                    backupParams.backupList == null
                        ? ImmutableList.of(backupParams)
                        : backupParams.backupList;
                boolean success;
                if (backup.category.equals(BackupCategory.YB_CONTROLLER)) {
                  success = ybcManager.deleteNfsDirectory(backup);
                } else {
                  success = deleteScriptBackup(backupList);
                }
                if (success) {
                  backup.delete();
                  log.info("Backup {} is successfully deleted", backupUUID);
                } else {
                  backup.transitionState(BackupState.FailedToDelete);
                }
              } else {
                backup.delete();
                log.info("NFS Backup {} is deleted as universe is not present", backup.backupUUID);
              }
              break;
            default:
              backup.transitionState(Backup.BackupState.FailedToDelete);
              log.error(
                  "Backup {} deletion failed due to invalid Config type {} provided",
                  backup.backupUUID,
                  customerConfig.name);
          }
        } catch (Exception e) {
          log.error(" Error in deleting backup " + backup.backupUUID.toString(), e);
          backup.transitionState(Backup.BackupState.FailedToDelete);
        }
      } else {
        log.error(
            "Error while deleting backup {} due to invalid storage config {} : {}",
            backup.backupUUID,
            storageConfigUUID);
        backup.transitionState(BackupState.FailedToDelete);
      }
    } catch (Exception e) {
      log.error("Error while deleting backup " + backup.backupUUID, e);
      backup.transitionState(BackupState.FailedToDelete);
    } finally {
      // Re-enable cert checking as it applies globally
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
    }
  }

  private Boolean isUniversePresent(Backup backup) {
    Optional<Universe> universe = Universe.maybeGet(backup.getBackupInfo().universeUUID);
    return universe.isPresent();
  }

  private boolean deleteScriptBackup(List<BackupTableParams> backupList) {
    boolean success = true;
    for (BackupTableParams childBackupParams : backupList) {
      if (!deleteChildScriptBackups(childBackupParams)) {
        success = false;
      }
    }
    return success;
  }

  private boolean deleteChildScriptBackups(BackupTableParams backupTableParams) {
    ShellResponse response = tableManagerYb.deleteBackup(backupTableParams);
    JsonNode jsonNode = null;
    try {
      jsonNode = Json.parse(response.message);
    } catch (Exception e) {
      log.error(
          "Delete Backup failed for {}. Response code={}, Output={}.",
          backupTableParams.storageLocation,
          response.code,
          response.message);
      return false;
    }
    if (response.code != 0 || jsonNode.has("error")) {
      log.error(
          "Delete Backup failed for {}. Response code={}, hasError={}.",
          backupTableParams.storageLocation,
          response.code,
          jsonNode.has("error"));
      return false;
    } else {
      log.info("Backup deleted successfully STDOUT: " + response.message);
      return true;
    }
  }

  private Boolean isCredentialUsable(CustomerConfig config) {
    Boolean isValid = true;
    try {
      backupUtil.validateStorageConfig(config);
    } catch (Exception e) {
      isValid = false;
    }
    return isValid;
  }
}
