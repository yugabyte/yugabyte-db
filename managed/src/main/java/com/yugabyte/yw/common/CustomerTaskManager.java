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
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class CustomerTaskManager {

  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskManager.class);

  public void failPendingTask(CustomerTask customerTask, TaskInfo taskInfo) {
    try {
      // Mark each subtask as a failure
      taskInfo
          .getIncompleteSubTasks()
          .forEach(
              subtask -> {
                subtask.setTaskState(TaskInfo.State.Failure);
                subtask.save();
              });
      // Mark task as a failure
      taskInfo.setTaskState(TaskInfo.State.Failure);
      JsonNode jsonNode = taskInfo.getTaskDetails();
      if (jsonNode != null && jsonNode instanceof ObjectNode) {
        ObjectNode details = (ObjectNode) jsonNode;
        details.put("errorString", "Platform restarted.");
      }
      taskInfo.save();
      // Mark customer task as completed
      customerTask.markAsCompleted();

      if (customerTask.getTarget().equals(TargetType.Backup)
          && customerTask.getType().equals(TaskType.Create)) {
        // Make transition state false for inProgress backups
        UUID taskUUID = taskInfo.getTaskUUID();
        List<Backup> backupList = Backup.fetchAllBackupsByTaskUUID(taskUUID);
        backupList
            .stream()
            .filter(backup -> backup.state.equals(Backup.BackupState.InProgress))
            .forEach(backup -> backup.transitionState(Backup.BackupState.Failed));
        // Create the update lambda.
        Universe.UniverseUpdater updater =
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              if (universeDetails.backupInProgress) {
                universeDetails.backupInProgress = false;
              }
              if (universeDetails.updateInProgress) {
                universeDetails.updateInProgress = false;
              }
              universe.setUniverseDetails(universeDetails);
            };

        Universe.saveDetails(customerTask.getTargetUUID(), updater, false);
        LOG.debug("Unlocked universe {} for backups.", customerTask.getTargetUUID());
      }

      // Unlock the universe for future operations
      if (customerTask.getTarget().equals(TargetType.Universe)) {
        // Create the update lambda.
        Universe.UniverseUpdater updater =
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              if (universeDetails.updateInProgress) {
                universeDetails.updateInProgress = false;
                universe.setUniverseDetails(universeDetails);
              }
            };

        Universe.saveDetails(customerTask.getTargetUUID(), updater, false);
        LOG.debug("Unlocked universe {} for updates.", customerTask.getTargetUUID());
      }
    } catch (Exception e) {
      LOG.error(String.format("Error encountered failing task %s", customerTask.getTaskUUID()), e);
    }
  }

  public void failAllPendingTasks() {
    LOG.info("Failing incomplete tasks...");
    try {
      String incompleteStates =
          String.join(
              "', '",
              new String[] {
                TaskInfo.State.Created.name(),
                TaskInfo.State.Initializing.name(),
                TaskInfo.State.Running.name(),
                TaskInfo.State.Abort.name()
              });
      // Retrieve all incomplete customer tasks.
      // TODO It is possible that completion_time == NULL
      // but task_state is completed. Retry will not appear on UI.
      String query =
          "SELECT ti.uuid AS task_uuid, ct.id AS customer_task_id "
              + "FROM task_info ti, customer_task ct "
              + "WHERE ti.uuid = ct.task_uuid "
              + "AND ct.completion_time IS NULL "
              + "AND ti.task_state IN ('"
              + incompleteStates
              + "')";
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
