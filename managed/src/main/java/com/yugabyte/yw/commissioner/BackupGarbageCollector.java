// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_NFS;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.TaskInfoManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Gauge;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Singleton
@Slf4j
public class BackupGarbageCollector {

  private final PlatformScheduler platformScheduler;

  private final TableManagerYb tableManagerYb;

  private final YbcManager ybcManager;

  private final CustomerConfigService customerConfigService;

  private final BackupHelper backupHelper;

  private final RuntimeConfGetter confGetter;

  private final TaskInfoManager taskInfoManager;

  private final Commissioner commissioner;

  private final StorageUtilFactory storageUtilFactory;

  private final ThreadPoolExecutor threadPool;

  private static final String YB_BACKUP_GARBAGE_COLLECTOR_INTERVAL = "yb.backupGC.gc_run_interval";
  private static final String AZ = Util.AZ;
  private static final String GCS = Util.GCS;
  private static final String S3 = Util.S3;
  private static final String NFS = Util.NFS;
  private static final int BACKUP_DELETION_MAX_RETRIES_COUNT = 3;

  public static final Gauge DELETE_BACKUP_FAILURE =
      Gauge.build("ybp_delete_backup_failure", "Count of failed delete backup attempt")
          .labelNames(MetricLabelsBuilder.CUSTOMER_LABELS)
          .register(CollectorRegistry.defaultRegistry);

  @Inject
  public BackupGarbageCollector(
      PlatformScheduler platformScheduler,
      CustomerConfigService customerConfigService,
      RuntimeConfGetter confGetter,
      TableManagerYb tableManagerYb,
      BackupHelper backupHelper,
      YbcManager ybcManager,
      TaskInfoManager taskInfoManager,
      Commissioner commissioner,
      StorageUtilFactory storageUtilFactory,
      PlatformExecutorFactory platformExecutorFactory) {
    this.platformScheduler = platformScheduler;
    this.customerConfigService = customerConfigService;
    this.confGetter = confGetter;
    this.tableManagerYb = tableManagerYb;
    this.backupHelper = backupHelper;
    this.ybcManager = ybcManager;
    this.taskInfoManager = taskInfoManager;
    this.commissioner = commissioner;
    this.storageUtilFactory = storageUtilFactory;
    this.threadPool =
        platformExecutorFactory.createExecutor(
            "backupGC", new ThreadFactoryBuilder().setNameFormat("backupGC-%d").build());
  }

  public void start() {
    Duration gcInterval = this.gcRunInterval();
    handleDeleteInProgressBackups();
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, gcInterval, this::scheduleRunner);
  }

  private Duration gcRunInterval() {
    return confGetter.getStaticConf().getDuration(YB_BACKUP_GARBAGE_COLLECTOR_INTERVAL);
  }

  private int getDeleteExpiredBackupMaxGCCount() {
    return confGetter.getGlobalConf(GlobalConfKeys.deleteExpiredBackupMaxGCSize);
  }

  public void handleDeleteInProgressBackups() {
    for (Customer customer : Customer.getAll()) {
      List<Backup> deleteInProgressBackups =
          Backup.findAllBackupWithState(
              customer.getUuid(), Collections.singletonList(BackupState.DeleteInProgress));
      if (deleteInProgressBackups != null) {
        deleteInProgressBackups.forEach(
            backup -> backup.transitionState(BackupState.QueuedForDeletion));
      }
    }
  }

  void scheduleRunner() {
    log.info("Running Backup Garbage Collector");
    try {
      List<Customer> customersList = Customer.getAll();

      for (Customer customer : customersList) {
        int failedToDeleteBackupCount = 0;
        // Delete the backups associated with customer storage config which are in
        // QueuedForDeletion or QueuedForForcedDeletion state.
        List<CustomerConfig> configList =
            CustomerConfig.getAllStorageConfigsQueuedForDeletion(customer.getUuid());
        for (CustomerConfig config : configList) {
          try {
            List<Future<Boolean>> backupFutures = new ArrayList<>();
            List<Backup> backupList =
                Backup.findAllBackupsQueuedForDeletionWithCustomerConfig(
                    config.getConfigUUID(), customer.getUuid());
            // Wait for all backup deletes to finish, and track failures
            log.info("Deleting {} backups for customer config {}", backupList.size(), config);
            for (Backup backup : backupList) {
              try {
                backupFutures.add(runDeleteBackup(customer, backup.getBackupUUID()));
              } catch (RejectedExecutionException e) {
                log.warn(
                    "threadpool for deleting backups is full, skipping until next garbage"
                        + " collection cycle",
                    e);
                break;
              }
            }
            // Wait for all backup deletes to finish, and track failures
            for (Future<Boolean> future : backupFutures) {
              try {
                if (!future.get()) {
                  failedToDeleteBackupCount++;
                }
              } catch (Exception e) {
                log.error("Error occurred while deleting backup", e);
                failedToDeleteBackupCount++;
              }
            }
          } catch (Exception e) {
            log.error(
                "Error occurred while deleting backups associated with {} storage config",
                config.getConfigName());
          } finally {
            config.delete();
            log.info("Customer Storage config {} is deleted", config.getConfigName());
          }
        }
        // Delete remaining backups queued for deletion.
        List<Future<Boolean>> backupFutures = new ArrayList<>();
        List<Backup> backupList = Backup.findAllBackupsQueuedForDeletion(customer.getUuid());
        if (!CollectionUtils.isEmpty(backupList)) {
          log.info("Deleting {} backups for customer {}", backupList.size(), customer.getUuid());
          for (Backup backup : backupList) {
            try {
              backupFutures.add(runDeleteBackup(customer, backup.getBackupUUID()));
            } catch (RejectedExecutionException e) {
              log.warn(
                  "threadpool for deleting backups is full, skipping the rest until next garbage"
                      + " collection cycle",
                  e);
              break;
            }
          }
        }
        for (Future<Boolean> future : backupFutures) {
          try {
            if (!future.get()) {
              failedToDeleteBackupCount++;
            }
          } catch (Exception e) {
            log.error("Error occurred while deleting backup", e);
            failedToDeleteBackupCount++;
          }
        }
        MetricLabelsBuilder metricLabelsBuilder =
            MetricLabelsBuilder.create().appendCustomer(customer).appendSource(customer);
        DELETE_BACKUP_FAILURE
            .labels(metricLabelsBuilder.getPrometheusValues())
            .set(failedToDeleteBackupCount);
      }
      // Create task to delete expired backups.
      Map<UUID, List<Backup>> expiredBackups = Backup.getCompletedExpiredBackups();
      expiredBackups.forEach(
          (customerUUID, backups) -> {
            deleteExpiredBackupsForCustomer(customerUUID, backups);
          });
    } catch (Exception e) {
      log.error("Error running backup garbage collector", e);
    }
  }

  private void deleteExpiredBackupsForCustomer(UUID customerUUID, List<Backup> expiredBackups) {
    Map<UUID, List<Backup>> expiredBackupsPerSchedule = new HashMap<>();
    List<Backup> backupsToDelete = new ArrayList<>();
    for (Backup backup : expiredBackups) {
      UUID scheduleUUID = backup.getScheduleUUID();
      Optional<Schedule> optionalSchedule =
          (scheduleUUID != null) ? Schedule.maybeGet(scheduleUUID) : Optional.empty();
      if (!optionalSchedule.isPresent()) {
        backupsToDelete.add(backup);
      } else {
        if (!expiredBackupsPerSchedule.containsKey(scheduleUUID)) {
          expiredBackupsPerSchedule.put(scheduleUUID, new ArrayList<>());
        }
        expiredBackupsPerSchedule.get(scheduleUUID).add(backup);
      }
    }
    for (UUID scheduleUUID : expiredBackupsPerSchedule.keySet()) {
      backupsToDelete.addAll(
          getBackupsToDeleteForSchedule(
              customerUUID, scheduleUUID, expiredBackupsPerSchedule.get(scheduleUUID)));
    }

    // Only delete certain number of expired backups in an iteration.
    int maxGCCount = getDeleteExpiredBackupMaxGCCount();
    if (backupsToDelete.size() > maxGCCount) {
      backupsToDelete = backupsToDelete.subList(0, maxGCCount);
    }

    for (Backup backup : backupsToDelete) {
      if (checkValidStorageConfig(backup)) {
        this.runDeleteBackupTask(customerUUID, backup);
      } else {
        log.error(
            "Cannot delete expired backup {} as storage config {} does not exists",
            backup.getBackupUUID(),
            backup.getStorageConfigUUID());
        backup.transitionState(BackupState.FailedToDelete);
      }
    }
  }

  private List<Backup> getBackupsToDeleteForSchedule(
      UUID customerUUID, UUID scheduleUUID, List<Backup> expiredBackups) {
    List<Backup> backupsToDelete = new ArrayList<Backup>();
    int minNumBackupsToRetain = Util.MIN_NUM_BACKUPS_TO_RETAIN,
        totalBackupsCount =
            Backup.fetchAllCompletedBackupsByScheduleUUID(customerUUID, scheduleUUID).size();
    Schedule schedule = Schedule.maybeGet(scheduleUUID).orElse(null);
    if (schedule != null && schedule.getTaskParams().has("minNumBackupsToRetain")) {
      minNumBackupsToRetain = schedule.getTaskParams().get("minNumBackupsToRetain").intValue();
    }
    backupsToDelete.addAll(
        expiredBackups.stream()
            .filter(backup -> !backup.getState().equals(BackupState.Completed))
            .collect(Collectors.toList()));
    expiredBackups.removeIf(backup -> !backup.getState().equals(BackupState.Completed));
    int numBackupsToDelete =
        Math.min(expiredBackups.size(), Math.max(0, totalBackupsCount - minNumBackupsToRetain));
    if (numBackupsToDelete > 0) {
      Collections.sort(
          expiredBackups,
          new Comparator<Backup>() {
            @Override
            public int compare(Backup b1, Backup b2) {
              return b1.getCreateTime().compareTo(b2.getCreateTime());
            }
          });
      for (int i = 0; i < Math.min(numBackupsToDelete, expiredBackups.size()); i++) {
        backupsToDelete.add(expiredBackups.get(i));
      }
    }
    return backupsToDelete;
  }

  private void runDeleteBackupTask(UUID customerUUID, Backup backup) {
    if (Backup.IN_PROGRESS_STATES.contains(backup.getState())) {
      log.warn("Cannot delete backup {} since it is in a progress state", backup.getBackupUUID());
      return;
    } else if (taskInfoManager.isDeleteBackupTaskAlreadyPresent(
        customerUUID, backup.getBackupUUID())) {
      log.warn(
          "Cannot delete backup {} since a delete backup task is already present",
          backup.getBackupUUID());
      return;
    }
    DeleteBackupYb.Params taskParams = new DeleteBackupYb.Params();
    taskParams.customerUUID = customerUUID;
    taskParams.backupUUID = backup.getBackupUUID();
    UUID taskUUID = commissioner.submit(TaskType.DeleteBackupYb, taskParams);
    String target =
        !StringUtils.isEmpty(backup.getUniverseName())
            ? backup.getUniverseName()
            : String.format("univ-%s", backup.getUniverseUUID().toString());
    log.info(
        "Submitted task to delete expired backup {}, task uuid = {}.",
        backup.getBackupUUID(),
        taskUUID);
    CustomerTask.create(
        Customer.get(customerUUID),
        backup.getBackupUUID(),
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Delete,
        target,
        null,
        "Expired Backup Deletion");
  }

  /**
   * Submits an async task to delete a backup for the specified customer and backup UUID.
   *
   * @param customer The customer for whom the backup is to be deleted.
   * @param backupUUID The unique identifier of the backup to be deleted.
   * @return A Future representing the result of the backup deletion operation.
   * @throws RejectedExecutionException If the task cannot be scheduled for execution.
   */
  private Future<Boolean> runDeleteBackup(Customer customer, UUID backupUUID) {
    return threadPool.submit(() -> deleteBackup(customer, backupUUID));
  }

  public boolean deleteBackup(Customer customer, UUID backupUUID) {
    Backup backup = Backup.maybeGet(customer.getUuid(), backupUUID).orElse(null);
    int backupDeleteRetryCount =
        confGetter.getConfForScope(customer, CustomerConfKeys.backupGcNumberOfRetries) - 1;
    // Backup is already deleted.
    if (backup == null || backup.getState() == BackupState.Deleted) {
      if (backup != null) {
        backup.delete();
      }
      return true;
    }
    boolean deletedSuccessfully = false,
        deleteForcefully = backup.getState().equals(BackupState.QueuedForForcedDeletion);
    try {
      UUID storageConfigUUID = backup.getBackupInfo().storageConfigUUID;
      CustomerConfig customerConfig =
          customerConfigService.getOrBadRequest(backup.getCustomerUUID(), storageConfigUUID);
      BackupCategory category = backup.getCategory();
      if (isCredentialUsable(customerConfig, backup.getUniverseUUID(), category)) {
        Map<String, List<String>> backupLocationsMap = null;
        log.info("Backup {} deletion started", backupUUID);
        backup.transitionState(BackupState.DeleteInProgress);
        switch (customerConfig.getName()) {
            // for cases S3, AZ, GCS, we get Util from CloudUtil class
          case S3:
          case GCS:
          case AZ:
            CloudUtil cloudUtil = storageUtilFactory.getCloudUtil(customerConfig.getName());
            backupLocationsMap = BackupUtil.getBackupLocations(backup);
            int numRetries = 0;
            long sleepTimeInMilliSeconds = 5000;
            while (numRetries < BACKUP_DELETION_MAX_RETRIES_COUNT && !deletedSuccessfully) {
              if (cloudUtil.deleteKeyIfExists(
                      customerConfig.getDataObject(),
                      backupLocationsMap.get(YbcBackupUtil.DEFAULT_REGION_STRING).get(0))
                  && cloudUtil.deleteStorage(customerConfig.getDataObject(), backupLocationsMap)) {
                deletedSuccessfully = true;
              }
              if (!deletedSuccessfully) {
                Thread.sleep(sleepTimeInMilliSeconds);
                sleepTimeInMilliSeconds = sleepTimeInMilliSeconds * 2;
              }
              numRetries++;
            }
            if (deletedSuccessfully) {
              backup.delete();
              log.info("Backup {} is successfully deleted", backupUUID);
            } else {
              int retryCount = backup.getRetryCount();
              if (retryCount < backupDeleteRetryCount) {
                retryCount++;
                backup.setRetryCount(retryCount);
                backup.transitionState(Backup.BackupState.QueuedForDeletion);
                log.info("Backup {} deletion failed on attempt {}", backupUUID, retryCount);
              } else {
                backup.transitionState(Backup.BackupState.FailedToDelete);
                log.info("Backup {} deletion failed", backupUUID);
              }
            }
            break;
          case NFS:
            if (isUniversePresent(backup)) {
              List<BackupTableParams> backupList = backup.getBackupParamsCollection();
              boolean success;
              if (backup.getCategory().equals(BackupCategory.YB_CONTROLLER)) {
                success = ybcManager.deleteNfsDirectory(backup);
              } else {
                success = deleteScriptBackup(backupList);
              }
              if (success) {
                deletedSuccessfully = true;
                backup.delete();
                log.info("Backup {} is successfully deleted", backupUUID);
              } else {
                int retryCount = backup.getRetryCount();
                if (retryCount < backupDeleteRetryCount) {
                  retryCount++;
                  backup.setRetryCount(retryCount);
                  backup.transitionState(Backup.BackupState.QueuedForDeletion);
                  log.info("Backup {} deletion failed on attempt {}", backupUUID, retryCount);
                } else {
                  backup.transitionState(Backup.BackupState.FailedToDelete);
                  log.info("Backup {} deletion failed", backupUUID);
                }
              }
            } else {
              deletedSuccessfully = true;
              backup.delete();
              log.info(
                  "NFS Backup {} is deleted as universe is not present", backup.getBackupUUID());
            }
            break;
          default:
            backup.transitionState(Backup.BackupState.FailedToDelete);
            log.error(
                "Backup {} deletion failed due to invalid Config type {} provided",
                backup.getBackupUUID(),
                customerConfig.getName());
        }
      } else {
        log.error(
            "Error while deleting backup {} due to invalid storage config {}",
            backup.getBackupUUID(),
            storageConfigUUID);
        backup.transitionState(BackupState.FailedToDelete);
      }
    } catch (Exception e) {
      log.error("Error while deleting backup " + backup.getBackupUUID(), e);
      backup.transitionState(BackupState.FailedToDelete);
      deletedSuccessfully = false;
    }
    if (!deletedSuccessfully) {
      if (!deleteForcefully) {
        return false;
      } else {
        log.info("Deleted backup {} forcefully", backupUUID);
        backup.delete();
      }
    }
    return true;
  }

  private Boolean isUniversePresent(Backup backup) {
    Optional<Universe> universe = Universe.maybeGet(backup.getBackupInfo().getUniverseUUID());
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

  private Boolean isCredentialUsable(
      CustomerConfig config, UUID universeUUID, BackupCategory category) {
    Boolean isValid = true;
    try {
      if (config.getName().equals(NAME_NFS)) {
        Optional<Universe> universeOpt = Universe.maybeGet(universeUUID);

        if (universeOpt.isPresent()) {
          if (category.equals(BackupCategory.YB_CONTROLLER)) {
            backupHelper.validateStorageConfigForBackupOnUniverse(config, universeOpt.get());
          } else {
            storageUtilFactory
                .getStorageUtil(config.getName())
                .validateStorageConfigOnUniverseNonRpc(config, universeOpt.get());
          }
        }

      } else {
        backupHelper.validateStorageConfig(config);
      }
    } catch (Exception e) {
      log.error(
          "Storage config {} to use for backup deletion failed validation: {}",
          config.getConfigUUID(),
          e.getMessage());
      isValid = false;
    }
    return isValid;
  }

  private boolean checkValidStorageConfig(Backup backup) {
    try {
      backupHelper.validateStorageConfigOnBackup(backup);
    } catch (Exception e) {
      return false;
    }
    log.debug(
        "Successfully validated storage config {} assigned to backup {}",
        backup.getStorageConfigUUID(),
        backup.getBackupUUID());
    return true;
  }
}
