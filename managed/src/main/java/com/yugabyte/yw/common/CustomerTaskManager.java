// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.RESTORE_BACKUP_CUSTOMER_TASK_FILE;
import static com.yugabyte.yw.common.Util.RESTORE_BACKUP_TASK_FILE;
import static io.ebean.DB.beginTransaction;
import static io.ebean.DB.commitTransaction;
import static io.ebean.DB.endTransaction;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.commissioner.tasks.CloudProviderEdit;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.commissioner.tasks.PauseUniverse;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyKubernetesClusterDelete;
import com.yugabyte.yw.commissioner.tasks.RebootNodeInUniverse;
import com.yugabyte.yw.commissioner.tasks.ResumeUniverse;
import com.yugabyte.yw.commissioner.tasks.SendUserNotification;
import com.yugabyte.yw.commissioner.tasks.params.IProviderTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.YsqlQueryExecutor.ConsistencyInfoResp;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.FileDataService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.AuditLogConfigParams;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesToggleImmutableYbcParams;
import com.yugabyte.yw.forms.MetricsExportConfigParams;
import com.yugabyte.yw.forms.QueryLogConfigParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.forms.YbcGflagsTaskParams;
import com.yugabyte.yw.forms.YbcThrottleTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PendingConsistencyCheck;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import io.ebean.DB;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.client.ChangeLoadBalancerStateResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@Singleton
@Slf4j
public class CustomerTaskManager {

  private final Commissioner commissioner;
  private final YBClientService ybService;
  private final YbcManager ybcManager;
  private final YsqlQueryExecutor ysqlQueryExecutor;
  private final RuntimeConfGetter confGetter;
  private final FileDataService fileDataService;
  private final ReleaseManager releaseManager;
  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskManager.class);
  private static final List<TaskType> LOAD_BALANCER_TASK_TYPES =
      Arrays.asList(
          TaskType.RestoreBackup,
          TaskType.BackupUniverse,
          TaskType.MultiTableBackup,
          TaskType.CreateBackup);
  private static final String ALTER_LOAD_BALANCER = "alterLoadBalancer";

  @Inject
  public CustomerTaskManager(
      YBClientService ybService,
      Commissioner commissioner,
      YbcManager ybcManager,
      YsqlQueryExecutor ysqlQueryExecutor,
      RuntimeConfGetter confGetter,
      FileDataService fileDataService,
      ReleaseManager releaseManager,
      SoftwareUpgradeHelper softwareUpgradeHelper) {
    this.ybService = ybService;
    this.commissioner = commissioner;
    this.ybcManager = ybcManager;
    this.ysqlQueryExecutor = ysqlQueryExecutor;
    this.confGetter = confGetter;
    this.fileDataService = fileDataService;
    this.releaseManager = releaseManager;
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  // Invoked if the task is in incomplete state.
  private void setTaskError(TaskInfo taskInfo) {
    taskInfo.setTaskState(TaskInfo.State.Failure);
    YBAError taskError = taskInfo.getTaskError();
    if (taskError == null) {
      taskError = new YBAError(Code.PLATFORM_RESTARTED, "Platform restarted.");
      taskInfo.setTaskError(taskError);
    }
  }

  public void handlePendingTask(CustomerTask customerTask, TaskInfo taskInfo) {
    try {
      // Mark each subtask as a failure if it is not completed.
      taskInfo
          .getIncompleteSubTasks()
          .forEach(
              subtask -> {
                setTaskError(subtask);
                subtask.save();
              });

      Optional<Universe> optUniv =
          (customerTask.getTargetType().isUniverseTarget()
                  || customerTask.getTargetType().equals(TargetType.Backup))
              ? Universe.maybeGet(customerTask.getTargetUUID())
              : Optional.empty();
      if (LOAD_BALANCER_TASK_TYPES.contains(taskInfo.getTaskType())) {
        Boolean isLoadBalanceAltered = false;
        JsonNode node = taskInfo.getTaskParams();
        if (node.has(ALTER_LOAD_BALANCER)) {
          isLoadBalanceAltered = node.path(ALTER_LOAD_BALANCER).asBoolean(false);
        }
        if (optUniv.isPresent() && isLoadBalanceAltered) {
          enableLoadBalancer(optUniv.get());
        }
      }

      UUID taskUUID = taskInfo.getUuid();
      ScheduleTask scheduleTask = ScheduleTask.fetchByTaskUUID(taskUUID);
      if (scheduleTask != null) {
        scheduleTask.markCompleted();
      }

      // Use isUniverseTarget() instead of directly comparing with Universe type because some
      // targets like Cluster, Node are Universe targets.
      boolean unlockUniverse = customerTask.getTargetType().isUniverseTarget();
      boolean resumeTask = false;
      boolean isRestoreYbc = false;
      CustomerTask.TaskType type = customerTask.getType();
      Map<BackupCategory, List<Backup>> backupCategoryMap = new HashMap<>();
      if (customerTask.getTargetType().equals(TargetType.Backup)) {
        // Backup is not universe target.
        if (CustomerTask.TaskType.Create.equals(type)) {
          // Make transition state false for inProgress backups
          List<Backup> backupList = Backup.fetchAllBackupsByTaskUUID(taskUUID);
          backupCategoryMap =
              backupList.stream()
                  .filter(
                      backup ->
                          backup.getState().equals(Backup.BackupState.InProgress)
                              || backup.getState().equals(Backup.BackupState.Stopped))
                  .collect(Collectors.groupingBy(Backup::getCategory));

          backupCategoryMap
              .getOrDefault(BackupCategory.YB_BACKUP_SCRIPT, new ArrayList<>())
              .stream()
              .forEach(backup -> backup.transitionState(Backup.BackupState.Failed));
          List<Backup> ybcBackups =
              backupCategoryMap.getOrDefault(BackupCategory.YB_CONTROLLER, new ArrayList<>());
          if (!optUniv.isPresent()) {
            ybcBackups.stream()
                .forEach(backup -> backup.transitionState(Backup.BackupState.Failed));
          } else {
            if (!ybcBackups.isEmpty()) {
              resumeTask = true;
            }
          }
          unlockUniverse = true;
        } else if (CustomerTask.TaskType.Delete.equals(type)) {
          // NOOP because Delete does not lock Universe.
        } else if (CustomerTask.TaskType.Restore.equals(type)) {
          // Restore locks the Universe.
          unlockUniverse = true;
          RestoreBackupParams params =
              Json.fromJson(taskInfo.getTaskParams(), RestoreBackupParams.class);
          if (params.category.equals(BackupCategory.YB_CONTROLLER)) {
            isRestoreYbc = true;
          }
        }
      } else if (CustomerTask.TaskType.Restore.equals(type)) {
        unlockUniverse = true;
        RestoreBackupParams params =
            Json.fromJson(taskInfo.getTaskParams(), RestoreBackupParams.class);
        if (params.category.equals(BackupCategory.YB_CONTROLLER)) {
          resumeTask = true;
          isRestoreYbc = true;
        }
      } else if (CustomerTask.TaskType.SendUserNotification.equals(type)) {
        resumeTask = true;
      }

      if (!isRestoreYbc) {
        List<Restore> restoreList =
            Restore.fetchByTaskUUID(taskUUID).stream()
                .filter(
                    restore ->
                        restore.getState().equals(Restore.State.Created)
                            || restore.getState().equals(Restore.State.InProgress))
                .collect(Collectors.toList());
        for (Restore restore : restoreList) {
          restore.update(taskUUID, Restore.State.Failed);
          RestoreKeyspace.update(restore, TaskInfo.State.Failure);
        }
      }

      if (unlockUniverse) {
        // Unlock the universe for future operations.
        optUniv.ifPresent(
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              if (details.updateInProgress) {
                // Create the update lambda.
                Universe.UniverseUpdater updater =
                    universe -> {
                      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
                      universeDetails.updateInProgress = false;
                      universe.setUniverseDetails(universeDetails);
                    };

                Universe.saveDetails(customerTask.getTargetUUID(), updater, false);
                log.debug("Unlocked universe {}.", customerTask.getTargetUUID());
              }
            });
      }

      // Mark task as a failure after the universe is unlocked.
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        setTaskError(taskInfo);
        taskInfo.save();
      }

      // Resume tasks if any
      TaskType taskType = taskInfo.getTaskType();
      AbstractTaskParams taskParams = null;
      log.info("Resume Task: {}", resumeTask);

      try {
        if (resumeTask) {
          if (optUniv.isPresent()) {
            Universe universe = optUniv.get();
            if (!taskUUID.equals(universe.getUniverseDetails().updatingTaskUUID)) {
              log.debug("Invalid task state: Task {} cannot be resumed", taskUUID);
              customerTask.markAsCompleted();
              return;
            }
          }
          switch (taskType) {
            case CreateBackup:
              BackupRequestParams backupParams =
                  Json.fromJson(taskInfo.getTaskParams(), BackupRequestParams.class);
              taskParams = backupParams;
              break;
            case RestoreBackup:
              RestoreBackupParams restoreParams =
                  Json.fromJson(taskInfo.getTaskParams(), RestoreBackupParams.class);
              taskParams = restoreParams;
              break;
            case SendUserNotification:
              SendUserNotification.Params sendParams =
                  Json.fromJson(taskInfo.getTaskParams(), SendUserNotification.Params.class);
              taskParams = sendParams;
              break;
            default:
              log.error("Invalid task type: {} during platform restart", taskType);
              return;
          }
          taskParams.setPreviousTaskUUID(taskUUID);
          taskInfo
              .getSubTasks()
              .forEach(
                  subtask -> {
                    subtask.delete();
                  });
          AbstractTaskParams finalTaskParams = taskParams;
          Util.doWithCorrelationId(
              corrId -> {
                // There is a chance that async execution is delayed and correlation ID is
                // overwritten. It is rare because there is no queuing of tasks.
                UUID newTaskUUID = commissioner.submit(taskType, finalTaskParams, taskUUID);
                beginTransaction();
                try {
                  customerTask.updateTaskUUID(newTaskUUID);
                  customerTask.resetCompletionTime();
                  customerTask.setCorrelationId(corrId);
                  commitTransaction();
                  return customerTask;
                } catch (Exception e) {
                  throw new RuntimeException(
                      "Unable to delete the previous task info: " + taskUUID);
                } finally {
                  endTransaction();
                }
              });

        } else {
          // Mark customer task as completed.
          // Customer task is marked completed after the task state is updated in TaskExecutor.
          // Moreover, this method has an internal guard to set the completion time only if it is
          // null.
          customerTask.markAsCompleted();
        }
      } catch (Exception ex) {
        customerTask.markAsCompleted();
        throw ex;
      }
    } catch (Exception e) {
      log.error(String.format("Error encountered failing task %s", customerTask.getTaskUUID()), e);
    }
  }

  public void handleAllPendingTasks() {
    log.info("Handle the pending tasks...");
    try {
      String incompleteStates =
          TaskInfo.INCOMPLETE_STATES.stream()
              .map(Objects::toString)
              .collect(Collectors.joining("','"));

      // Retrieve all incomplete customer tasks or task in incomplete state. Task state update
      // and customer completion time update are not transactional.
      String query =
          "SELECT ti.uuid AS task_uuid, ct.id AS customer_task_id "
              + "FROM task_info ti, customer_task ct "
              + "WHERE ti.uuid = ct.task_uuid "
              + "AND (ct.completion_time IS NULL "
              + "OR ti.task_state IN ('"
              + incompleteStates
              + "'))";
      // TODO use Finder.
      DB.sqlQuery(query)
          .findList()
          .forEach(
              row -> {
                TaskInfo taskInfo = TaskInfo.getOrBadRequest(row.getUUID("task_uuid"));
                CustomerTask customerTask = CustomerTask.get(row.getLong("customer_task_id"));
                handlePendingTask(customerTask, taskInfo);
              });
      for (Customer customer : Customer.getAll()) {
        // Change the DeleteInProgress backups state to QueuedForDeletion
        Backup.findAllBackupWithState(
                customer.getUuid(), Arrays.asList(Backup.BackupState.DeleteInProgress))
            .stream()
            .forEach(b -> b.transitionState(Backup.BackupState.QueuedForDeletion));
        // Update intermediate schedules to Error state and clear running state
        Schedule.getAllByCustomerUUIDAndType(customer.getUuid(), TaskType.CreateBackup).stream()
            .forEach(
                s -> {
                  if (s.isRunningState()) {
                    s.setRunningState(false /* runningState */);
                  }
                  if (s.getStatus().isIntermediateState()) {
                    s.setStatus(Schedule.State.Error);
                  }
                  s.save();
                });
      }
    } catch (Exception e) {
      log.error("Encountered error failing pending tasks", e);
    }
  }

  public void handleRestoreTask() {
    Path restoreFilePath = Paths.get(AppConfigHelper.getStoragePath(), RESTORE_BACKUP_TASK_FILE);
    Path restoreCustomerTaskFilePath =
        Paths.get(AppConfigHelper.getStoragePath(), RESTORE_BACKUP_CUSTOMER_TASK_FILE);
    if (Files.exists(restoreCustomerTaskFilePath) && Files.exists(restoreFilePath)) {
      try {
        TaskInfo restoreTaskInfo =
            Json.mapper().readValue(restoreFilePath.toFile(), TaskInfo.class);
        Optional<TaskInfo> existingTask = TaskInfo.maybeGet(restoreTaskInfo.getUuid());
        if (existingTask.isEmpty()) {
          restoreTaskInfo.setTaskState(TaskInfo.State.Success);
          restoreTaskInfo.save();
        }
        CustomerTask customerTask =
            Json.mapper().readValue(restoreCustomerTaskFilePath.toFile(), CustomerTask.class);
        if (customerTask == null) {
          log.warn("Restore customer task is null, skipping.");
          return;
        }
        Optional<CustomerTask> existingCustomerTask =
            CustomerTask.maybeGet(customerTask.getTaskUUID());
        if (existingCustomerTask.isEmpty()) {
          customerTask.markAsCompleted();
          customerTask.save();
        }
      } catch (IOException e) {
        log.warn("Could not read restore customer task error {}, skipping.", e.getMessage());
      } finally {
        try {
          Files.deleteIfExists(restoreCustomerTaskFilePath);
          Files.deleteIfExists(restoreFilePath);
        } catch (IOException e) {
          log.warn("Failed to delete restore backup task file {}", e.getMessage());
        }
      }
      fileDataService.fixUpPaths(AppConfigHelper.getStoragePath());
      releaseManager.fixFilePaths();
    }
  }

  /**
   * Updates the state of the universe in the event that the most recent task performed on it was an
   * upgrade task that failed or was aborted which is called on YBA startup.
   */
  public void updateUniverseSoftwareUpgradeStateSet() {
    Set<UUID> universeUUIDSet = Universe.getAllUUIDs();
    for (UUID uuid : universeUUIDSet) {
      Universe universe = Universe.getOrBadRequest(uuid);
      Customer customer = Customer.get(universe.getCustomerId());
      UUID placementModificationTaskUuid =
          universe.getUniverseDetails().placementModificationTaskUuid;
      if (placementModificationTaskUuid != null) {
        CustomerTask placementModificationTask =
            CustomerTask.getOrBadRequest(customer.getUuid(), placementModificationTaskUuid);
        SoftwareUpgradeState state =
            getUniverseSoftwareUpgradeStateBasedOnTask(universe, placementModificationTask);
        if (!UniverseDefinitionTaskParams.IN_PROGRESS_UNIV_SOFTWARE_UPGRADE_STATES.contains(
            universe.getUniverseDetails().softwareUpgradeState)) {
          log.debug("Skipping universe upgrade state as actual task was not started.");
        } else {
          universe.updateUniverseSoftwareUpgradeState(state);
          log.debug("Updated universe {} software upgrade state to  {}.", uuid, state);
        }
      }
    }
  }

  private SoftwareUpgradeState getUniverseSoftwareUpgradeStateBasedOnTask(
      Universe universe, CustomerTask customerTask) {
    SoftwareUpgradeState state = universe.getUniverseDetails().softwareUpgradeState;
    Optional<TaskInfo> taskInfo = TaskInfo.maybeGet(customerTask.getTaskUUID());
    if (taskInfo.isPresent()) {
      TaskInfo lastTaskInfo = taskInfo.get();
      if (lastTaskInfo.getTaskState().equals(TaskInfo.State.Failure)
          || lastTaskInfo.getTaskState().equals(TaskInfo.State.Aborted)) {
        TaskType taskType = lastTaskInfo.getTaskType();
        if (Arrays.asList(TaskType.RollbackUpgrade, TaskType.RollbackKubernetesUpgrade)
            .contains(taskType)) {
          state = SoftwareUpgradeState.RollbackFailed;
        } else if (Arrays.asList(TaskType.FinalizeKubernetesUpgrade, TaskType.FinalizeUpgrade)
            .contains(taskType)) {
          state = SoftwareUpgradeState.FinalizeFailed;
        } else if (Arrays.asList(
                TaskType.SoftwareUpgrade,
                TaskType.SoftwareUpgradeYB,
                TaskType.SoftwareKubernetesUpgrade,
                TaskType.SoftwareKubernetesUpgradeYB)
            .contains(taskType)) {
          state = SoftwareUpgradeState.UpgradeFailed;
        }
      }
    }
    return state;
  }

  private void enableLoadBalancer(Universe universe) {
    ChangeLoadBalancerStateResponse resp = null;
    try (YBClient client = ybService.getUniverseClient(universe)) {
      resp = client.changeLoadBalancerState(true);
    } catch (Exception e) {
      log.error(
          "Setting load balancer to state true has failed for universe: {}",
          universe.getUniverseUUID());
    }

    if (resp != null && resp.hasError()) {
      log.error(
          "Setting load balancer to state true has failed for universe: {}",
          universe.getUniverseUUID());
    }
  }

  public void handleAutoRetryAbortedTasks() {
    if (HighAvailabilityConfig.isFollower()) {
      log.info("Skipping auto-retry of tasks because this YBA is a follower");
      return;
    }
    Duration timeWindow =
        confGetter.getGlobalConf(GlobalConfKeys.autoRetryTasksOnYbaRestartTimeWindow);
    if (timeWindow.isZero()) {
      log.debug("Skipping auto retry of aborted tasks due to YBA shutdown");
      return;
    }
    autoRetryAbortedTasks(
        timeWindow,
        c ->
            Util.doWithCorrelationId(
                corrId -> retryCustomerTask(c.getCustomerUUID(), c.getTaskUUID())));
  }

  @VisibleForTesting
  void autoRetryAbortedTasks(Duration timeWindow, Consumer<CustomerTask> retryFunc) {
    log.debug(
        "Auto retrying aborted tasks within time window of {} secs, due to YBA shutdown",
        timeWindow.getSeconds());
    Set<UUID> targetUuids = new HashSet<>();
    TaskInfo.getRecentParentTasksInStates(Collections.singleton(TaskInfo.State.Aborted), timeWindow)
        .stream()
        .filter(t -> StringUtils.isNotEmpty(t.getYbaVersion()))
        .filter(t -> YBAError.Code.PLATFORM_SHUTDOWN == t.getTaskError().getCode())
        .filter(t -> Commissioner.isTaskTypeRetryable(t.getTaskType()))
        .filter(
            t ->
                Util.areYbVersionsEqual(
                    Util.getYbaVersion(), t.getYbaVersion(), true /* suppressFormatError */))
        .forEach(
            t -> {
              CustomerTask.maybeGet(t.getUuid())
                  .ifPresent(
                      c -> {
                        try {
                          if (c.getCompletionTime() == null) {
                            log.debug(
                                "Task {}({}) is already running", t.getTaskType(), t.getUuid());
                            return;
                          }
                          if (targetUuids.contains(c.getTargetUUID())) {
                            log.info(
                                "Retry already submitted for target {}({})",
                                c.getTargetUUID(),
                                c.getTargetType());
                            return;
                          }
                          if (!isTaskRetryable(c, t)) {
                            log.debug(
                                "Task {}({}) is not retryable on target {}({})",
                                t.getTaskType(),
                                t.getUuid(),
                                c.getTargetUUID(),
                                c.getTargetType());
                            return;
                          }
                          targetUuids.add(c.getTargetUUID());
                          retryFunc.accept(c);
                        } catch (Exception e) {
                          // Ignore as it is best effort.
                          log.warn(
                              "Failed to retry task {}({}) on target {}({}) for customer {} - {}",
                              t.getTaskType(),
                              t.getUuid(),
                              c.getTargetName(),
                              c.getTargetType(),
                              c.getCustomerUUID(),
                              e.getMessage());
                        }
                      });
            });
  }

  private boolean isTaskRetryable(CustomerTask task, TaskInfo taskInfo) {
    return commissioner.isTaskRetryable(
        taskInfo,
        tf -> {
          if (task.getTargetType() == CustomerTask.TargetType.Provider) {
            CustomerTask lastTask = CustomerTask.getLastTaskByTargetUuid(task.getTargetUUID());
            return lastTask != null && lastTask.getTaskUUID().equals(task.getTaskUUID());
          }

          JsonNode xClusterConfigNode = taskInfo.getTaskParams().get("xClusterConfig");
          if (xClusterConfigNode != null && !xClusterConfigNode.isNull()) {
            XClusterConfig xClusterConfig = Json.fromJson(xClusterConfigNode, XClusterConfig.class);
            return isXClusterTaskRetryable(tf.getUuid(), xClusterConfig);
          }

          // Rest are all tasks on universe, but the target may be a node or backup etc.
          JsonNode node = taskInfo.getTaskParams().get("universeUUID");
          if (node == null || node.isNull()) {
            return false;
          }
          Optional<Universe> optional = Universe.maybeGet(UUID.fromString(node.asText()));
          if (!optional.isPresent()) {
            return false;
          }
          return tf.getUuid().equals(optional.get().getUniverseDetails().updatingTaskUUID)
              || tf.getUuid()
                  .equals(optional.get().getUniverseDetails().placementModificationTaskUuid);
        });
  }

  private boolean canTaskRollback(TaskInfo taskInfo) {
    return commissioner.canTaskRollback(taskInfo);
  }

  // This performs actual retryability check on the task parameters.
  private String verifyTaskRetryability(CustomerTask customerTask, AbstractTaskParams taskParams) {
    UUID retriedTaskUuid = customerTask.getTaskUUID();
    UUID targetUUID = customerTask.getTargetUUID();
    if (taskParams instanceof XClusterConfigTaskParams) {
      XClusterConfig xClusterConfig = ((XClusterConfigTaskParams) taskParams).getXClusterConfig();
      if (!isXClusterTaskRetryable(retriedTaskUuid, xClusterConfig)) {
        return "Invalid task state: Task " + retriedTaskUuid + " cannot be retried";
      }
    } else if (taskParams instanceof UniverseTaskParams) {
      Universe universe = Universe.getOrBadRequest(targetUUID);
      if (!retriedTaskUuid.equals(universe.getUniverseDetails().updatingTaskUUID)
          && !retriedTaskUuid.equals(universe.getUniverseDetails().placementModificationTaskUuid)) {
        return String.format("Invalid task state: Task %s cannot be retried", retriedTaskUuid);
      }
      CommonUtils.maybeGetUserFromContext()
          .ifPresent(user -> ((UniverseTaskParams) taskParams).creatingUser = user);
    } else if (taskParams instanceof IProviderTaskParams) {
      // Parallel execution is guarded by ProviderEditRestrictionManager
      CustomerTask lastTask = CustomerTask.getLastTaskByTargetUuid(targetUUID);
      if (lastTask == null || !lastTask.getId().equals(customerTask.getId())) {
        return "Only the last existing task can be retried";
      }
    } else {
      return "Unknown type for task params " + taskParams;
    }
    return null;
  }

  public static boolean isXClusterTaskRetryable(
      UUID retriedTaskUuid, @Nullable XClusterConfig xClusterConfig) {
    // For xCluster tasks, check both universes for the task uuid.
    if (Objects.nonNull(xClusterConfig)) {
      Optional<Universe> sourceUniverseOptional =
          Universe.maybeGet(xClusterConfig.getSourceUniverseUUID());
      Optional<Universe> targetUniverseOptional =
          Universe.maybeGet(xClusterConfig.getTargetUniverseUUID());
      if (sourceUniverseOptional.isPresent()
          && (retriedTaskUuid.equals(
                  sourceUniverseOptional.get().getUniverseDetails().updatingTaskUUID)
              || retriedTaskUuid.equals(
                  sourceUniverseOptional
                      .get()
                      .getUniverseDetails()
                      .placementModificationTaskUuid))) {
        return true;
      }
      if (targetUniverseOptional.isPresent()
          && (retriedTaskUuid.equals(
                  targetUniverseOptional.get().getUniverseDetails().updatingTaskUUID)
              || retriedTaskUuid.equals(
                  targetUniverseOptional
                      .get()
                      .getUniverseDetails()
                      .placementModificationTaskUuid))) {
        return true;
      }
    }
    return false;
  }

  public CustomerTask rollbackCustomerTask(UUID customerUUID, UUID taskUUID) {
    CustomerTask customerTask = CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    TaskInfo taskInfo = customerTask.getTaskInfo();
    JsonNode oldTaskParams = commissioner.getTaskParams(taskUUID);
    TaskType taskType = taskInfo.getTaskType();
    log.info(
        "Will rollback task {}, of type {} in {} state.",
        taskUUID,
        taskType,
        taskInfo.getTaskState());
    if (!canTaskRollback(taskInfo)) {
      String errMsg = String.format("Invalid task: Task %s cannot be rolled back", taskUUID);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    AbstractTaskParams taskParams;
    CustomerTask.TaskType customerTaskType;
    if (Objects.requireNonNull(taskType) == TaskType.SwitchoverDrConfig) {
      taskParams = Json.fromJson(oldTaskParams, DrConfigTaskParams.class);
      DrConfigTaskParams drConfigTaskParams = (DrConfigTaskParams) taskParams;
      drConfigTaskParams.refreshIfExists();
      taskType = TaskType.SwitchoverDrConfigRollback;
      customerTaskType = CustomerTask.TaskType.SwitchoverRollback;

      // Roll back cannot be done if the old xCluster config is partially or fully deleted.
      XClusterConfig currentXClusterConfig = drConfigTaskParams.getOldXClusterConfig();
      if (Objects.isNull(currentXClusterConfig)
          || !currentXClusterConfig.getTables().stream()
              .allMatch(XClusterTableConfig::isReplicationSetupDone)) {
        // At this point, the replication group on the new primary is deleted and it is
        // possible that the user has written data to the new primary, so setting up
        // replication from the new primary to the old primary is not safe and might need
        // bootstrapping which cannot be done in the rollback of the switchover.
        throw new PlatformServiceException(
            BAD_REQUEST,
            "The old xCluster config or its associated replication group is deleted and cannot do a"
                + " roll back; At this point the user is able to write to the new primary universe."
                + " You may retry the switchover task. If your intention is make the new primary"
                + " universe the dr universe again, you can run another switchover task.");
      }
      log.debug("Rolling back switchover task with old xCluster config: {}", currentXClusterConfig);
    } else {
      String errMsg =
          String.format(
              "Invalid task type: %s cannot be rolled back, the task is annotated to be able to"
                  + " roll back but the logic to compute the taskParams is not implemented",
              taskType);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }
    if (Objects.isNull(customerTaskType)) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "CustomerTaskType is null");
    }

    // Reset the error string.
    taskParams.setErrorString(null);
    taskParams.setPreviousTaskUUID(taskUUID);
    UUID newTaskUUID = commissioner.submit(taskType, taskParams);
    log.info(
        "Submitted rollback task for target {}:{}, task uuid = {}.",
        customerTask.getTargetUUID(),
        customerTask.getTargetName(),
        newTaskUUID);
    return CustomerTask.create(
        customer,
        customerTask.getTargetUUID(),
        newTaskUUID,
        customerTask.getTargetType(),
        customerTaskType,
        customerTask.getTargetName());
  }

  public CustomerTask retryCustomerTask(UUID customerUUID, UUID taskUUID) {
    CustomerTask customerTask = CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    TaskInfo taskInfo = customerTask.getTaskInfo();
    JsonNode oldTaskParams = commissioner.getTaskParams(taskUUID);
    TaskType taskType = taskInfo.getTaskType();
    log.info(
        "Will retry task {}, of type {} in {} state.", taskUUID, taskType, taskInfo.getTaskState());
    if (!isTaskRetryable(customerTask, taskInfo)) {
      String errMsg = String.format("Invalid task: Task %s cannot be retried", taskUUID);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    AbstractTaskParams taskParams = null;
    switch (taskType) {
      case CreateKubernetesUniverse:
      case CreateUniverse:
      case EditUniverse:
      case InstallYbcSoftwareOnK8s:
      case EditKubernetesUniverse:
      case ReadOnlyKubernetesClusterCreate:
      case ReadOnlyClusterCreate:
      case SyncMasterAddresses:
        taskParams = Json.fromJson(oldTaskParams, UniverseDefinitionTaskParams.class);
        break;
      case ResizeNode:
        taskParams = Json.fromJson(oldTaskParams, ResizeNodeParams.class);
        break;
      case DestroyKubernetesUniverse:
        taskParams = Json.fromJson(oldTaskParams, DestroyUniverse.Params.class);
        break;
      case KubernetesOverridesUpgrade:
        taskParams = Json.fromJson(oldTaskParams, KubernetesOverridesUpgradeParams.class);
        break;
      case GFlagsUpgrade:
        taskParams = Json.fromJson(oldTaskParams, GFlagsUpgradeParams.class);
        GFlagsUpgradeParams gFlagsUpgradeParams = (GFlagsUpgradeParams) taskParams;
        if (gFlagsUpgradeParams != null && gFlagsUpgradeParams.getUniverseUUID() != null) {
          Universe universe = Universe.getOrBadRequest(gFlagsUpgradeParams.getUniverseUUID());
          if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(universe)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Cannot retry GFlags upgrade task as YSQL major upgrade is in progress.");
          }
        }
        break;
      case GFlagsKubernetesUpgrade:
        taskParams = Json.fromJson(oldTaskParams, KubernetesGFlagsUpgradeParams.class);
        KubernetesGFlagsUpgradeParams kubeGFlagsUpgradeParams =
            (KubernetesGFlagsUpgradeParams) taskParams;
        if (kubeGFlagsUpgradeParams != null && kubeGFlagsUpgradeParams.getUniverseUUID() != null) {
          Universe universe = Universe.getOrBadRequest(kubeGFlagsUpgradeParams.getUniverseUUID());
          if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(universe)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Cannot retry GFlags upgrade task as YSQL major upgrade is in progress.");
          }
        }
        break;
      case SoftwareKubernetesUpgradeYB:
      case SoftwareKubernetesUpgrade:
      case SoftwareUpgrade:
      case SoftwareUpgradeYB:
        taskParams = Json.fromJson(oldTaskParams, SoftwareUpgradeParams.class);
        break;
      case UpdateKubernetesDiskSize:
        taskParams = Json.fromJson(oldTaskParams, ResizeNodeParams.class);
        break;
      case FinalizeUpgrade:
      case FinalizeKubernetesUpgrade:
        taskParams = Json.fromJson(oldTaskParams, FinalizeUpgradeParams.class);
        break;
      case RollbackUpgrade:
      case RollbackKubernetesUpgrade:
        taskParams = Json.fromJson(oldTaskParams, RollbackUpgradeParams.class);
        break;
      case VMImageUpgrade:
        taskParams = Json.fromJson(oldTaskParams, VMImageUpgradeParams.class);
        break;
      case RestartUniverse:
        taskParams = Json.fromJson(oldTaskParams, RestartTaskParams.class);
        break;
      case RestartUniverseKubernetesUpgrade:
        taskParams = Json.fromJson(oldTaskParams, RestartTaskParams.class);
        break;
      case RebootUniverse:
        taskParams = Json.fromJson(oldTaskParams, UpgradeTaskParams.class);
        break;
      case ThirdpartySoftwareUpgrade:
        taskParams = Json.fromJson(oldTaskParams, ThirdpartySoftwareUpgradeParams.class);
        break;
      case CertsRotate:
      case CertsRotateKubernetesUpgrade:
        taskParams = Json.fromJson(oldTaskParams, CertsRotateParams.class);
        break;
      case TlsToggle:
      case TlsToggleKubernetes:
        taskParams = Json.fromJson(oldTaskParams, TlsToggleParams.class);
        break;
      case SystemdUpgrade:
        taskParams = Json.fromJson(oldTaskParams, SystemdUpgradeParams.class);
        break;
      case KubernetesToggleImmutableYbc:
        taskParams = Json.fromJson(oldTaskParams, KubernetesToggleImmutableYbcParams.class);
        break;
      case UpdateK8sYbcThrottleFlags:
      case UpdateYbcThrottleFlags:
        taskParams = Json.fromJson(oldTaskParams, YbcThrottleTaskParams.class);
        break;
      case UpgradeYbcGFlags:
      case UpgradeKubernetesYbcGFlags:
        taskParams = Json.fromJson(oldTaskParams, YbcGflagsTaskParams.class);
        break;
      case ModifyAuditLoggingConfig:
        taskParams = Json.fromJson(oldTaskParams, AuditLogConfigParams.class);
        AuditLogConfigParams auditLogConfigParams = (AuditLogConfigParams) taskParams;
        if (auditLogConfigParams != null && auditLogConfigParams.getUniverseUUID() != null) {
          Universe universe = Universe.getOrBadRequest(auditLogConfigParams.getUniverseUUID());
          if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(universe)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Cannot retry modifying audit logging task as YSQL major upgrade is in progress.");
          }
        }
        break;
      case ModifyQueryLoggingConfig:
        taskParams = Json.fromJson(oldTaskParams, QueryLogConfigParams.class);
        QueryLogConfigParams queryLogConfigParams = (QueryLogConfigParams) taskParams;
        if (queryLogConfigParams != null && queryLogConfigParams.getUniverseUUID() != null) {
          Universe universe = Universe.getOrBadRequest(queryLogConfigParams.getUniverseUUID());
          if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(universe)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Cannot retry modifying query logging task as YSQL major upgrade is in progress.");
          }
        }
      case ModifyMetricsExportConfig:
        taskParams = Json.fromJson(oldTaskParams, MetricsExportConfigParams.class);
        break;
      case AddNodeToUniverse:
      case RemoveNodeFromUniverse:
      case DeleteNodeFromUniverse:
      case ReleaseInstanceFromUniverse:
      case RebootNodeInUniverse:
      case StartNodeInUniverse:
      case StopNodeInUniverse:
      case StartMasterOnNode:
      case ReprovisionNode:
      case MasterFailover:
        String nodeName = oldTaskParams.get("nodeName").textValue();
        String universeUUIDStr = oldTaskParams.get("universeUUID").textValue();
        UUID universeUUID = UUID.fromString(universeUUIDStr);
        // Build node task params for node actions.
        NodeTaskParams nodeTaskParams = new NodeTaskParams();
        if (taskType == TaskType.RebootNodeInUniverse) {
          nodeTaskParams = new RebootNodeInUniverse.Params();
          ((RebootNodeInUniverse.Params) nodeTaskParams).isHardReboot =
              oldTaskParams.get("isHardReboot").asBoolean();
        }
        nodeTaskParams.nodeName = nodeName;
        nodeTaskParams.setUniverseUUID(universeUUID);

        // Populate the user intent for software upgrades like gFlag upgrades.
        Universe universe = Universe.getOrBadRequest(universeUUID, customer);
        nodeTaskParams.clusters.addAll(universe.getUniverseDetails().clusters);

        nodeTaskParams.expectedUniverseVersion = -1;
        if (oldTaskParams.has("rootCA")) {
          nodeTaskParams.rootCA = UUID.fromString(oldTaskParams.get("rootCA").textValue());
        }
        if (universe.isYbcEnabled()) {
          nodeTaskParams.setEnableYbc(true);
          nodeTaskParams.setYbcInstalled(true);
          nodeTaskParams.setYbcSoftwareVersion(ybcManager.getStableYbcVersion());
        }
        if (taskType == TaskType.MasterFailover) {
          nodeTaskParams.azUuid = UUID.fromString(oldTaskParams.get("azUuid").textValue());
        }
        taskParams = nodeTaskParams;
        break;
      case ReplaceNodeInUniverse:
      case DecommissionNode:
        // TODO: Revisit to avoid sending the whole payload.
        nodeTaskParams = Json.fromJson(oldTaskParams, NodeTaskParams.class);
        nodeName = oldTaskParams.get("nodeName").textValue();
        nodeTaskParams.nodeName = nodeName;
        taskParams = nodeTaskParams;
        break;
      case BackupUniverse:
        // V1 Restore Task
        universeUUIDStr = oldTaskParams.get("universeUUID").textValue();
        universeUUID = UUID.fromString(universeUUIDStr);
        // Build restore V1 task params for restore task.
        BackupTableParams backupTableParams = new BackupTableParams();
        backupTableParams.setUniverseUUID(universeUUID);
        backupTableParams.customerUuid = customer.getUuid();
        backupTableParams.actionType =
            BackupTableParams.ActionType.valueOf(oldTaskParams.get("actionType").textValue());
        backupTableParams.storageConfigUUID =
            UUID.fromString((oldTaskParams.get("storageConfigUUID").textValue()));
        backupTableParams.storageLocation = oldTaskParams.get("storageLocation").textValue();
        backupTableParams.backupType =
            TableType.valueOf(oldTaskParams.get("backupType").textValue());
        String restore_keyspace = oldTaskParams.get("keyspace").textValue();
        backupTableParams.setKeyspace(restore_keyspace);
        if (oldTaskParams.has("parallelism")) {
          backupTableParams.parallelism = oldTaskParams.get("parallelism").asInt();
        }
        if (oldTaskParams.has("disableChecksum")) {
          backupTableParams.disableChecksum = oldTaskParams.get("disableChecksum").asBoolean();
        }
        if (oldTaskParams.has("useTablespaces")) {
          backupTableParams.useTablespaces = oldTaskParams.get("useTablespaces").asBoolean();
        }
        taskParams = backupTableParams;
        break;
      case MultiTableBackup:
        // V1 Backup task
        universeUUIDStr = oldTaskParams.get("universeUUID").textValue();
        universeUUID = UUID.fromString(universeUUIDStr);
        // Build backup task params for backup actions.
        MultiTableBackup.Params multiTableParams = new MultiTableBackup.Params();
        multiTableParams.setUniverseUUID(universeUUID);
        multiTableParams.actionType =
            BackupTableParams.ActionType.valueOf(oldTaskParams.get("actionType").textValue());
        multiTableParams.storageConfigUUID =
            UUID.fromString((oldTaskParams.get("storageConfigUUID").textValue()));
        multiTableParams.backupType =
            TableType.valueOf(oldTaskParams.get("backupType").textValue());
        multiTableParams.customerUUID = customer.getUuid();
        if (oldTaskParams.has("keyspace")) {
          String backup_keyspace = oldTaskParams.get("keyspace").textValue();
          multiTableParams.setKeyspace(backup_keyspace);
        }
        if (oldTaskParams.has("tableUUIDList")) {
          JsonNode tableUUIDListJson = oldTaskParams.get("tableUUIDList");
          if (tableUUIDListJson.isArray()) {
            for (final JsonNode objNode : tableUUIDListJson) {
              multiTableParams.tableUUIDList.add(UUID.fromString(String.valueOf(objNode)));
            }
          }
        }
        if (oldTaskParams.has("parallelism")) {
          multiTableParams.parallelism = oldTaskParams.get("parallelism").asInt();
        }
        if (oldTaskParams.has("transactionalBackup")) {
          multiTableParams.transactionalBackup =
              oldTaskParams.get("transactionalBackup").asBoolean();
        }
        if (oldTaskParams.has("sse")) {
          multiTableParams.sse = oldTaskParams.get("sse").asBoolean();
        }
        if (oldTaskParams.has("useTablespaces")) {
          multiTableParams.useTablespaces = oldTaskParams.get("useTablespaces").asBoolean();
        }
        if (oldTaskParams.has("disableChecksum")) {
          multiTableParams.disableChecksum = oldTaskParams.get("disableChecksum").asBoolean();
        }
        if (oldTaskParams.has("disableParallelism")) {
          multiTableParams.disableParallelism = oldTaskParams.get("disableParallelism").asBoolean();
        }

        taskParams = multiTableParams;
        break;
      case CloudProviderDelete:
        taskParams = Json.fromJson(oldTaskParams, CloudProviderDelete.Params.class);
        break;
      case CloudBootstrap:
        taskParams = Json.fromJson(oldTaskParams, CloudBootstrap.Params.class);
        break;
      case CloudProviderEdit:
        taskParams = Json.fromJson(oldTaskParams, CloudProviderEdit.Params.class);
        break;
      case ReadOnlyKubernetesClusterDelete:
        taskParams = Json.fromJson(oldTaskParams, ReadOnlyKubernetesClusterDelete.Params.class);
        break;
      case ReadOnlyClusterDelete:
        taskParams = Json.fromJson(oldTaskParams, ReadOnlyClusterDelete.Params.class);
        break;
      case FailoverDrConfig:
      case SwitchoverDrConfig:
      case SwitchoverDrConfigRollback:
      case DeleteDrConfig:
      case CreateDrConfig:
      case EditDrConfig:
      case SetDatabasesDrConfig:
      case SetTablesDrConfig:
        taskParams = Json.fromJson(oldTaskParams, DrConfigTaskParams.class);
        DrConfigTaskParams drConfigTaskParams = (DrConfigTaskParams) taskParams;
        drConfigTaskParams.refreshIfExists();
        if (taskType != TaskType.DeleteDrConfig) {
          if (drConfigTaskParams.getSourceUniverseUuid() != null) {
            Universe sourceUniverse =
                Universe.getOrBadRequest(drConfigTaskParams.getSourceUniverseUuid());
            if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(sourceUniverse)) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot retry DR config task as YSQL major upgrade is in progress.");
            }
          }
          if (drConfigTaskParams.getTargetUniverseUuid() != null) {
            Universe targetUniverse =
                Universe.getOrBadRequest(drConfigTaskParams.getTargetUniverseUuid());
            if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(targetUniverse)) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot retry DR config task as YSQL major upgrade is in progress.");
            }
          }
        }
        // Todo: we need to recompute other task param fields here to handle changes in the database
        //  at the YBDB level, e.g., the user creates a table after the task has filed and before it
        //  is retried.
        break;
      case DeleteXClusterConfig:
      case CreateXClusterConfig:
      case EditXClusterConfig:
        taskParams = Json.fromJson(oldTaskParams, XClusterConfigTaskParams.class);
        XClusterConfigTaskParams xClusterConfigTaskParams = (XClusterConfigTaskParams) taskParams;
        xClusterConfigTaskParams.refreshIfExists();
        break;
      case PauseUniverse:
        taskParams = Json.fromJson(oldTaskParams, PauseUniverse.Params.class);
        break;
      case ResumeUniverse:
        taskParams = Json.fromJson(oldTaskParams, ResumeUniverse.Params.class);
        break;
      default:
        String errMsg =
            String.format(
                "Invalid task type: %s. Only Universe, some Node task retries are supported.",
                taskType);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Reset the error string.
    taskParams.setErrorString(null);
    taskParams.setPreviousTaskUUID(taskUUID);
    String errMsg = verifyTaskRetryability(customerTask, taskParams);
    if (errMsg != null) {
      log.error("Task {} cannot be retried - {}", taskUUID, errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    UUID newTaskUUID = commissioner.submit(taskType, taskParams);
    log.info(
        "Submitted retry task to universe for {}:{}, task uuid = {}.",
        customerTask.getTargetUUID(),
        customerTask.getTargetName(),
        newTaskUUID);
    return CustomerTask.create(
        customer,
        customerTask.getTargetUUID(),
        newTaskUUID,
        customerTask.getTargetType(),
        customerTask.getType(),
        customerTask.getTargetName());
  }

  public static String getCustomTaskName(
      CustomerTask.TaskType customerTaskType,
      UniverseTaskParams taskParams,
      String currentCustomTaskName) {
    if (taskParams.isRunOnlyPrechecks()) {
      String baseName =
          currentCustomTaskName == null
              ? customerTaskType.getFriendlyName()
              : currentCustomTaskName;
      return "Validation " + baseName;
    }
    return currentCustomTaskName;
  }

  public void handlePendingConsistencyTasks() {
    List<PendingConsistencyCheck> pendings = PendingConsistencyCheck.getAll();

    for (PendingConsistencyCheck pending : pendings) {
      try {
        Universe universe = Universe.getOrBadRequest(pending.getUniverse().getUniverseUUID());
        ConsistencyInfoResp response = ysqlQueryExecutor.getConsistencyInfo(universe);
        if (response != null) {
          UUID dbTaskUuid = response.getTaskUUID();
          int dbSeqNum = response.getSeqNum();
          if (dbTaskUuid.equals(pending.getTaskUuid())) {
            // Updated on DB side before crash, set ourselves to whatever is in the DB
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.sequenceNumber = dbSeqNum;
            universe.setUniverseDetails(universeDetails);
            universe.save(false);
          } else if (dbSeqNum > universe.getUniverseDetails().sequenceNumber) {
            log.warn("Found pending task on a universe that appears stale.");
          }
        }
      } catch (Exception e) {
        log.warn("Exception handling WAL tasks: " + e.getMessage());
      } finally {
        pending.delete();
      }
    }
  }
}
