// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TargetType;
import static com.yugabyte.yw.models.CustomerTask.TaskType;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import io.ebean.Ebean;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class CustomerTaskManager {

  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskManager.class);

  public void failPendingTask(CustomerTask customerTask, TaskInfo taskInfo) {
    try {
      // Mark each subtask as a failure if it is not completed.
      taskInfo
          .getIncompleteSubTasks()
          .forEach(
              subtask -> {
                subtask.setTaskState(TaskInfo.State.Failure);
                subtask.save();
              });

      boolean unlockUniverse = TargetType.Universe.equals(customerTask.getTarget());
      if (customerTask.getTarget().equals(TargetType.Backup)
          && customerTask.getType().equals(TaskType.Create)) {
        // Make transition state false for inProgress backups
        UUID taskUUID = taskInfo.getTaskUUID();
        List<Backup> backupList = Backup.fetchAllBackupsByTaskUUID(taskUUID);
        backupList
            .stream()
            .filter(backup -> backup.state.equals(Backup.BackupState.InProgress))
            .forEach(backup -> backup.transitionState(Backup.BackupState.Failed));
        unlockUniverse = true;
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
        taskInfo.setTaskState(TaskInfo.State.Failure);
        JsonNode jsonNode = taskInfo.getTaskDetails();
        if (jsonNode instanceof ObjectNode) {
          ObjectNode details = (ObjectNode) jsonNode;
          details.put("errorString", "Platform restarted.");
        }
        taskInfo.save();
      }
      // Mark customer task as completed.
      // Customer task is marked completed after the task state is updated in TaskExecutor.
      // Moreover, this method has an internal guard to set the completion time only if it is null.
      customerTask.markAsCompleted();
    } catch (Exception e) {
      LOG.error(String.format("Error encountered failing task %s", customerTask.getTaskUUID()), e);
    }
  }

  public void failAllPendingTasks() {
    LOG.info("Failing incomplete tasks...");
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
              + "'))";
      // TODO use Finder.
      Ebean.createSqlQuery(query)
          .findList()
          .forEach(
              row -> {
                TaskInfo taskInfo = TaskInfo.get(row.getUUID("task_uuid"));
                CustomerTask customerTask = CustomerTask.get(row.getLong("customer_task_id"));
                failPendingTask(customerTask, taskInfo);
              });
    } catch (Exception e) {
      LOG.error("Encountered error failing pending tasks", e);
    }
  }
}
