// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TargetType;
import static io.ebean.Ebean.beginTransaction;
import static io.ebean.Ebean.commitTransaction;
import static io.ebean.Ebean.endTransaction;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.DB;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeLoadBalancerStateResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@Singleton
public class CustomerTaskManager {

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
  public CustomerTaskManager(YBClientService ybService, Commissioner commissioner) {
    this.ybService = ybService;
    this.commissioner = commissioner;
  }

  private void setTaskError(TaskInfo taskInfo) {
    taskInfo.setTaskState(TaskInfo.State.Failure);
    JsonNode jsonNode = taskInfo.getDetails();
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

      Optional<Universe> optUniv =
          customerTask.getTargetType().isUniverseTarget()
              ? Universe.maybeGet(customerTask.getTargetUUID())
              : Optional.empty();
      if (LOAD_BALANCER_TASK_TYPES.contains(taskInfo.getTaskType())) {
        Boolean isLoadBalanceAltered = false;
        JsonNode node = taskInfo.getDetails();
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

          backupCategoryMap.getOrDefault(BackupCategory.YB_BACKUP_SCRIPT, new ArrayList<>())
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
              Json.fromJson(taskInfo.getDetails(), RestoreBackupParams.class);
          if (params.category.equals(BackupCategory.YB_CONTROLLER)) {
            isRestoreYbc = true;
          }
        }
      } else if (CustomerTask.TaskType.Restore.equals(type)) {
        unlockUniverse = true;
        RestoreBackupParams params =
            Json.fromJson(taskInfo.getDetails(), RestoreBackupParams.class);
        if (params.category.equals(BackupCategory.YB_CONTROLLER)) {
          resumeTask = true;
          isRestoreYbc = true;
        }
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
      LOG.info("Resume Task: {}", resumeTask);

      try {
        if (resumeTask && optUniv.isPresent()) {
          Universe universe = optUniv.get();
          if (!taskUUID.equals(universe.getUniverseDetails().updatingTaskUUID)) {
            LOG.debug("Invalid task state: Task {} cannot be resumed", taskUUID);
            customerTask.markAsCompleted();
            return;
          }

          switch (taskType) {
            case CreateBackup:
              BackupRequestParams backupParams =
                  Json.fromJson(taskInfo.getDetails(), BackupRequestParams.class);
              taskParams = backupParams;
              break;
            case RestoreBackup:
              RestoreBackupParams restoreParams =
                  Json.fromJson(taskInfo.getDetails(), RestoreBackupParams.class);
              taskParams = restoreParams;
              break;
            default:
              LOG.error("Invalid task type: {} during platform restart", taskType);
              return;
          }
          taskParams.setPreviousTaskUUID(taskUUID);
          UUID newTaskUUID = commissioner.submit(taskType, taskParams);
          beginTransaction();
          try {
            customerTask.updateTaskUUID(newTaskUUID);
            customerTask.resetCompletionTime();
            Optional<TaskInfo> optional = TaskInfo.maybeGet(taskUUID);
            if (optional.isPresent()) {
              optional.get().getSubTasks().forEach(st -> st.delete());
              optional.get().delete();
            }
            commitTransaction();
          } catch (Exception e) {
            throw new RuntimeException("Unable to delete the previous task info: " + taskUUID);
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
          TaskInfo.INCOMPLETE_STATES.stream()
              .map(Objects::toString)
              .collect(Collectors.joining("','"));

      // Delete orphaned parent tasks which do not have any associated customer task.
      // It is rare but can happen as a customer task and task info are not saved in transaction.
      // TODO It can be handled better with transaction but involves bigger change.
      String query =
          "DELETE FROM task_info WHERE parent_uuid IS NULL AND uuid NOT IN "
              + "(SELECT task_uuid FROM customer_task)";
      DB.sqlUpdate(query).execute();

      // Retrieve all incomplete customer tasks or task in incomplete state. Task state update
      // and customer completion time update are not transactional.
      query =
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
      DB.sqlQuery(query)
          .findList()
          .forEach(
              row -> {
                TaskInfo taskInfo = TaskInfo.getOrBadRequest(row.getUUID("task_uuid"));
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
