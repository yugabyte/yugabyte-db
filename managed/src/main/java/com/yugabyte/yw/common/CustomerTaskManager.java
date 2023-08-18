// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TargetType;
import static io.ebean.DB.beginTransaction;
import static io.ebean.DB.commitTransaction;
import static io.ebean.DB.endTransaction;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.commissioner.tasks.RebootNodeInUniverse;
import com.yugabyte.yw.commissioner.tasks.params.IProviderTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
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
import org.yb.CommonTypes.TableType;
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

      Optional<Universe> optUniv = Universe.maybeGet(customerTask.getTargetUUID());
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
        Universe.maybeGet(customerTask.getTargetUUID())
            .ifPresent(
                u -> {
                  UniverseDefinitionTaskParams details = u.getUniverseDetails();
                  if (details.updateInProgress) {
                    // Create the update lambda.
                    Universe.UniverseUpdater updater =
                        universe -> {
                          UniverseDefinitionTaskParams universeDetails =
                              universe.getUniverseDetails();
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
                  Json.fromJson(taskInfo.getDetails(), BackupRequestParams.class);
              taskParams = backupParams;
              break;
            case RestoreBackup:
              RestoreBackupParams restoreParams =
                  Json.fromJson(taskInfo.getDetails(), RestoreBackupParams.class);
              taskParams = restoreParams;
              break;
            default:
              LOG.error(String.format("Invalid task type: %s during platform restart", taskType));
              return;
          }
          taskParams.setPreviousTaskUUID(taskUUID);
          UUID newTaskUUID = commissioner.submit(taskType, taskParams);
          beginTransaction();
          try {
            customerTask.updateTaskUUID(newTaskUUID);
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
          TaskInfo.INCOMPLETE_STATES.stream()
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
      DB.sqlQuery(query)
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

  public CustomerTask retryCustomerTask(UUID customerUUID, UUID taskUUID) {
    CustomerTask customerTask = CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    JsonNode oldTaskParams = commissioner.getTaskDetails(taskUUID);
    TaskType taskType = taskInfo.getTaskType();
    LOG.info(
        "Will retry task {}, of type {} in {} state.", taskUUID, taskType, taskInfo.getTaskState());
    if (!commissioner.isTaskRetryable(taskType)) {
      String errMsg = String.format("Invalid task type: Task %s cannot be retried", taskUUID);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    AbstractTaskParams taskParams = null;
    switch (taskType) {
      case CreateKubernetesUniverse:
      case CreateUniverse:
      case EditUniverse:
      case ReadOnlyClusterCreate:
        taskParams = Json.fromJson(oldTaskParams, UniverseDefinitionTaskParams.class);
        break;
      case ResizeNode:
        taskParams = Json.fromJson(oldTaskParams, ResizeNodeParams.class);
        break;
      case DestroyKubernetesUniverse:
        taskParams = Json.fromJson(oldTaskParams, DestroyUniverse.Params.class);
        break;
      case AddNodeToUniverse:
      case RemoveNodeFromUniverse:
      case DeleteNodeFromUniverse:
      case ReleaseInstanceFromUniverse:
      case RebootNodeInUniverse:
      case StartNodeInUniverse:
      case StopNodeInUniverse:
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
          nodeTaskParams.setYbcSoftwareVersion(
              universe.getUniverseDetails().getYbcSoftwareVersion());
        }
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
      default:
        String errMsg =
            String.format(
                "Invalid task type: %s. Only Universe, some Node task retries are supported.",
                taskType);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Reset the error string.
    taskParams.setErrorString(null);

    UUID targetUUID;
    String targetName;
    if (taskParams instanceof UniverseTaskParams) {
      targetUUID = ((UniverseTaskParams) taskParams).getUniverseUUID();
      Universe universe = Universe.getOrBadRequest(targetUUID);
      if (!taskUUID.equals(universe.getUniverseDetails().updatingTaskUUID)
          && !taskUUID.equals(universe.getUniverseDetails().placementModificationTaskUuid)) {
        String errMsg = String.format("Invalid task state: Task %s cannot be retried", taskUUID);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      targetName = universe.getName();
    } else if (taskParams instanceof IProviderTaskParams) {
      targetUUID = ((IProviderTaskParams) taskParams).getProviderUUID();
      Provider provider = Provider.getOrBadRequest(targetUUID);
      targetName = provider.getName();
      // Parallel execution is guarded by ProviderEditRestrictionManager
      CustomerTask lastTask = CustomerTask.getLastTaskByTargetUuid(targetUUID);
      if (lastTask == null || !lastTask.getId().equals(customerTask.getId())) {
        throw new PlatformServiceException(BAD_REQUEST, "Can restart only last task");
      }
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Unknown type for task params " + taskParams);
    }
    taskParams.setPreviousTaskUUID(taskUUID);
    UUID newTaskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted retry task to universe for {}:{}, task uuid = {}.",
        targetUUID,
        targetName,
        newTaskUUID);
    return CustomerTask.create(
        customer,
        targetUUID,
        newTaskUUID,
        customerTask.getTargetType(),
        customerTask.getType(),
        targetName);
  }
}
