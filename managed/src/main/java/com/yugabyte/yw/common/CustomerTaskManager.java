// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TargetType;
import static io.ebean.Ebean.beginTransaction;
import static io.ebean.Ebean.commitTransaction;
import static io.ebean.Ebean.endTransaction;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Ebean;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import java.time.Duration;
import javax.inject.Singleton;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeLoadBalancerStateResponse;
import org.yb.client.YBClient;
import play.api.Play;
import play.libs.Json;

@Singleton
public class CustomerTaskManager {

  private BackupUtil backupUtil;
  private Commissioner commissioner;
  private YBClientService ybService;

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
      BackupUtil backupUtil, YBClientService ybService, Commissioner commissioner) {
    this.backupUtil = backupUtil;
    this.ybService = ybService;
    this.commissioner = commissioner;
  }

  private void setTaskError(TaskInfo taskInfo) {
    taskInfo.setTaskState(TaskInfo.State.Failure);
    JsonNode jsonNode = taskInfo.getTaskDetails();
    if (jsonNode instanceof ObjectNode) {
      ObjectNode details = (ObjectNode) jsonNode;
      JsonNode errNode = details.get("errorString");
      if (errNode == null || errNode.isNull()) {
        details.put("errorString", "Platform restarted.");
      }
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

      Optional<Universe> optUniv = Universe.maybeGet(customerTask.getTargetUUID());
      if (LOAD_BALANCER_TASK_TYPES.contains(taskInfo.getTaskType())) {
        Boolean isLoadBalanceAltered = false;
        JsonNode node = taskInfo.getTaskDetails();
        if (node.has(ALTER_LOAD_BALANCER)) {
          isLoadBalanceAltered = node.path(ALTER_LOAD_BALANCER).asBoolean(false);
        }
        if (optUniv.isPresent() && isLoadBalanceAltered) {
          enableLoadBalancer(optUniv.get());
        }
      }

      UUID taskUUID = taskInfo.getTaskUUID();
      ScheduleTask scheduleTask = ScheduleTask.fetchByTaskUUID(taskUUID);
      if (scheduleTask != null) {
        scheduleTask.setCompletedTime();
      }

      // Use isUniverseTarget() instead of directly comparing with Universe type because some
      // targets like Cluster, Node are Universe targets.
      boolean unlockUniverse = customerTask.getTarget().isUniverseTarget();
      boolean resumeTask = false;
      CustomerTask.TaskType type = customerTask.getType();
      Map<BackupCategory, List<Backup>> backupCategoryMap = new HashMap<>();
      if (customerTask.getTarget().equals(TargetType.Backup)) {
        // Backup is not universe target.
        if (CustomerTask.TaskType.Create.equals(type)) {
          // Make transition state false for inProgress backups
          List<Backup> backupList = Backup.fetchAllBackupsByTaskUUID(taskUUID);
          backupCategoryMap =
              backupList
                  .stream()
                  .filter(
                      backup ->
                          backup.state.equals(Backup.BackupState.InProgress)
                              || backup.state.equals(Backup.BackupState.Stopped))
                  .collect(Collectors.groupingBy(Backup::getBackupCategory));

          backupCategoryMap
              .getOrDefault(BackupCategory.YB_BACKUP_SCRIPT, new ArrayList<>())
              .stream()
              .forEach(backup -> backup.transitionState(Backup.BackupState.Failed));
          List<Backup> ybcBackups =
              backupCategoryMap.getOrDefault(BackupCategory.YB_CONTROLLER, new ArrayList<>());
          if (!optUniv.isPresent()) {
            ybcBackups
                .stream()
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
          // Restore only locks the Universe but does not set backupInProgress flag.
          unlockUniverse = true;
        }
      } else if (CustomerTask.TaskType.Restore.equals(type)) {
        unlockUniverse = true;
        RestoreBackupParams params =
            Json.fromJson(taskInfo.getTaskDetails(), RestoreBackupParams.class);
        if (backupUtil.isYbcBackup(params.backupStorageInfoList.get(0).storageLocation)) {
          resumeTask = true;
        }
      }

      if (unlockUniverse) {
        // Unlock the universe for future operations.
        Universe.maybeGet(customerTask.getTargetUUID())
            .ifPresent(
                u -> {
                  UniverseDefinitionTaskParams details = u.getUniverseDetails();
                  if (details.backupInProgress || details.updateInProgress) {
                    // Create the update lambda.
                    Universe.UniverseUpdater updater =
                        universe -> {
                          UniverseDefinitionTaskParams universeDetails =
                              universe.getUniverseDetails();
                          universeDetails.backupInProgress = false;
                          universeDetails.updateInProgress = false;
                          universe.setUniverseDetails(universeDetails);
                        };

                    Universe.saveDetails(customerTask.getTargetUUID(), updater, false);
                    LOG.debug("Unlocked universe {}.", customerTask.getTargetUUID());
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
      UniverseTaskParams taskParams = null;
      LOG.info("Resume Task: " + String.valueOf(resumeTask));

      try {
        if (resumeTask && optUniv.isPresent()) {
          Universe universe = optUniv.get();
          if (!taskUUID.equals(universe.getUniverseDetails().updatingTaskUUID)) {
            String errMsg =
                String.format("Invalid task state: Task %s cannot be resumed", taskUUID);
            LOG.debug(errMsg);
            customerTask.markAsCompleted();
            return;
          }

          switch (taskType) {
            case CreateBackup:
              BackupRequestParams backupParams =
                  Json.fromJson(taskInfo.getTaskDetails(), BackupRequestParams.class);
              taskParams = backupParams;
              break;
            case RestoreBackup:
              RestoreBackupParams restoreParams =
                  Json.fromJson(taskInfo.getTaskDetails(), RestoreBackupParams.class);
              taskParams = restoreParams;
              break;
            default:
              LOG.error(String.format("Invalid task type: %s during platform restart", taskType));
          }
          taskParams.firstTry = false;
          taskParams.setPreviousTaskUUID(taskUUID);
          UUID newTaskUUID = commissioner.submit(taskType, taskParams);
          beginTransaction();
          try {
            customerTask.setTaskUUID(newTaskUUID);
            customerTask.resetCompletionTime();
            TaskInfo task = TaskInfo.get(taskUUID);
            if (task != null) {
              task.getSubTasks().forEach(st -> st.delete());
              task.delete();
            }
            commitTransaction();
          } catch (Exception e) {
            throw new RuntimeException(
                "Unable to delete the previous task info: " + taskUUID.toString());
          } finally {
            endTransaction();
          }

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
      LOG.error(String.format("Error encountered failing task %s", customerTask.getTaskUUID()), e);
    }
  }

  public void handleAllPendingTasks() {
    LOG.info("Handle the pending tasks...");
    try {
      String incompleteStates =
          TaskInfo.INCOMPLETE_STATES
              .stream()
              .map(Objects::toString)
              .collect(Collectors.joining("','"));
      // Retrieve all incomplete customer tasks or task in incomplete state. Task state update and
      // customer completion time update are not transactional.
      String query =
          "SELECT ti.uuid AS task_uuid, ct.id AS customer_task_id "
              + "FROM task_info ti, customer_task ct "
              + "WHERE ti.uuid = ct.task_uuid "
              + "AND (ct.completion_time IS NULL "
              + "OR ti.task_state IN ('"
              + incompleteStates
              + "') OR "
              + "(ti.task_state='Aborted' AND ti.details->>'errorString' = 'Platform shutdown'"
              + " AND ct.completion_time IS NULL))";
      // TODO use Finder.
      Ebean.createSqlQuery(query)
          .findList()
          .forEach(
              row -> {
                TaskInfo taskInfo = TaskInfo.get(row.getUUID("task_uuid"));
                CustomerTask customerTask = CustomerTask.get(row.getLong("customer_task_id"));
                handlePendingTask(customerTask, taskInfo);
              });
    } catch (Exception e) {
      LOG.error("Encountered error failing pending tasks", e);
    }
  }

  private void enableLoadBalancer(Universe universe) {
    ChangeLoadBalancerStateResponse resp = null;
    YBClient client = null;
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try {
      client = ybService.getClient(masterHostPorts, certificate);
      resp = client.changeLoadBalancerState(true);
    } catch (Exception e) {
      LOG.error(
          "Setting load balancer to state true has failed for universe: "
              + universe.getUniverseUUID());
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }
    if (resp != null && resp.hasError()) {
      LOG.error(
          "Setting load balancer to state true has failed for universe: "
              + universe.getUniverseUUID());
    }
  }
}
