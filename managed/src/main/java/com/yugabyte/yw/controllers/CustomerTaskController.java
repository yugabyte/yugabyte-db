// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Strings;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.commissioner.tasks.RebootNodeInUniverse;
import com.yugabyte.yw.commissioner.tasks.params.IProviderTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.FailedSubtasks;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.ExpressionList;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Customer Tasks",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerTaskController extends AuthenticatedController {

  @Inject private RuntimeConfGetter confGetter;
  @Inject private Commissioner commissioner;

  static final String CUSTOMER_TASK_DB_QUERY_LIMIT = "yb.customer_task_db_query_limit";
  private static final String YB_SOFTWARE_VERSION = "ybSoftwareVersion";
  private static final String YB_PREV_SOFTWARE_VERSION = "ybPrevSoftwareVersion";

  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  private List<SubTaskFormData> fetchFailedSubTasks(UUID parentUUID) {
    TaskInfo parentTask = TaskInfo.getOrBadRequest(parentUUID);
    ExpressionList<TaskInfo> subTaskQuery =
        TaskInfo.find
            .query()
            .where()
            .eq("parent_uuid", parentUUID)
            .in("task_state", TaskInfo.ERROR_STATES)
            .orderBy("position desc");
    LinkedList<TaskInfo> result = new LinkedList<>(subTaskQuery.findList());

    if (TaskInfo.ERROR_STATES.contains(parentTask.getTaskState()) && result.isEmpty()) {
      JsonNode taskError = parentTask.getDetails().get("errorString");
      if ((taskError != null) && !StringUtils.isEmpty(taskError.asText())) {
        // Parent task hasn't `sub_task_group_type` set but can have some error details
        // which are not present in subtasks. Usually these errors encountered on a
        // stage of the action preparation (some initial checks plus preparation of
        // subtasks for execution).
        if (parentTask.getSubTaskGroupType() == null) {
          parentTask.setSubTaskGroupType(SubTaskGroupType.Preparation);
        }
        result.addFirst(parentTask);
      }
    }

    List<SubTaskFormData> subTasks = new ArrayList<>(result.size());
    for (TaskInfo taskInfo : result) {
      SubTaskFormData subTaskData = new SubTaskFormData();
      subTaskData.subTaskUUID = taskInfo.getTaskUUID();
      subTaskData.subTaskType = taskInfo.getTaskType().name();
      subTaskData.subTaskState = taskInfo.getTaskState().name();
      subTaskData.creationTime = taskInfo.getCreateTime();
      subTaskData.subTaskGroupType = taskInfo.getSubTaskGroupType().name();
      JsonNode taskError = taskInfo.getDetails().get("errorString");
      subTaskData.errorString = (taskError == null) ? "null" : taskError.asText();
      subTasks.add(subTaskData);
    }
    return subTasks;
  }

  private CustomerTaskFormData buildCustomerTaskFromData(
      CustomerTask task, ObjectNode taskProgress, TaskInfo taskInfo) {
    try {
      CustomerTaskFormData taskData = new CustomerTaskFormData();
      taskData.percentComplete = taskProgress.get("percent").asInt();
      taskData.status = taskProgress.get("status").asText();
      taskData.abortable = taskProgress.get("abortable").asBoolean();
      taskData.retryable = taskProgress.get("retryable").asBoolean();
      taskData.id = task.getTaskUUID();
      taskData.title = task.getFriendlyDescription();
      taskData.createTime = task.getCreateTime();
      taskData.completionTime = task.getCompletionTime();
      taskData.target = task.getTargetType().name();
      taskData.type = task.getType().name();
      taskData.typeName =
          task.getCustomTypeName() != null
              ? task.getCustomTypeName()
              : task.getType().getFriendlyName();
      taskData.targetUUID = task.getTargetUUID();
      taskData.userEmail = task.getUserEmail();
      String correlationId = task.getCorrelationId();
      if (!Strings.isNullOrEmpty(correlationId)) taskData.correlationId = correlationId;
      ObjectNode versionNumbers = Json.newObject();
      JsonNode taskDetails = taskInfo.getDetails();
      if (task.getType() == CustomerTask.TaskType.SoftwareUpgrade
          && taskDetails.has(YB_PREV_SOFTWARE_VERSION)) {
        versionNumbers.put(
            YB_PREV_SOFTWARE_VERSION, taskDetails.get(YB_PREV_SOFTWARE_VERSION).asText());
        versionNumbers.put(YB_SOFTWARE_VERSION, taskDetails.get(YB_SOFTWARE_VERSION).asText());
        taskData.details = versionNumbers;
      }
      return taskData;
    } catch (RuntimeException e) {
      LOG.error("Error fetching task progress for {}. TaskInfo is not found", task.getTaskUUID());
      return null;
    }
  }

  private Map<UUID, List<CustomerTaskFormData>> fetchTasks(Customer customer, UUID targetUUID) {
    ExpressionList<CustomerTask> customerTaskQuery =
        CustomerTask.find
            .query()
            .where()
            .eq("customer_uuid", customer.getUuid())
            .orderBy("create_time desc");

    if (targetUUID != null) {
      customerTaskQuery.where().eq("target_uuid", targetUUID);
    }

    List<CustomerTask> customerTaskList =
        customerTaskQuery
            .setMaxRows(
                confGetter.getConfForScope(
                    Customer.getOrBadRequest(customer.getUuid()),
                    CustomerConfKeys.taskDbQueryLimit))
            .orderBy("create_time desc")
            .findPagedList()
            .getList();

    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();

    Set<UUID> taskUuids =
        customerTaskList.stream().map(CustomerTask::getTaskUUID).collect(Collectors.toSet());
    Map<UUID, TaskInfo> taskInfoMap =
        TaskInfo.find(taskUuids).stream()
            .collect(Collectors.toMap(TaskInfo::getTaskUUID, Function.identity()));
    Map<UUID, CustomerTask> lastTaskByTargetMap =
        customerTaskList.stream()
            .filter(c -> c.getCompletionTime() != null)
            .collect(
                Collectors.toMap(
                    CustomerTask::getTargetUUID,
                    Function.identity(),
                    (c1, c2) -> c1.getCompletionTime().after(c2.getCompletionTime()) ? c1 : c2));
    Map<UUID, String> updatingTaskByTargetMap =
        commissioner.getUpdatingTaskUUIDsForTargets(customer.getId());
    Map<UUID, String> placementModificationTaskByTargetMap =
        commissioner.getPlacementModificationTaskUUIDsForTargets(customer.getId());
    Map<UUID, Set<String>> allowRetryTasksByTargetMap = new HashMap<>();
    updatingTaskByTargetMap.forEach(
        (universeUUID, taskUUID) ->
            allowRetryTasksByTargetMap
                .computeIfAbsent(universeUUID, k -> new HashSet<>())
                .add(taskUUID));
    placementModificationTaskByTargetMap.forEach(
        (universeUUID, taskUUID) ->
            allowRetryTasksByTargetMap
                .computeIfAbsent(universeUUID, k -> new HashSet<>())
                .add(taskUUID));
    for (CustomerTask task : customerTaskList) {
      TaskInfo taskInfo = taskInfoMap.get(task.getTaskUUID());
      commissioner
          .buildTaskStatus(task, taskInfo, allowRetryTasksByTargetMap, lastTaskByTargetMap)
          .ifPresent(
              taskProgress -> {
                CustomerTaskFormData taskData =
                    buildCustomerTaskFromData(task, taskProgress, taskInfo);
                if (taskData != null) {
                  List<CustomerTaskFormData> taskList =
                      taskListMap.computeIfAbsent(task.getTargetUUID(), k -> new ArrayList<>());
                  taskList.add(taskData);
                }
              });
    }
    return taskListMap;
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result list(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customer, null);
    return PlatformResults.withData(taskList);
  }

  @ApiOperation(
      value = "List task",
      response = CustomerTaskFormData.class,
      responseContainer = "List")
  public Result tasksList(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    List<CustomerTaskFormData> flattenList = new ArrayList<CustomerTaskFormData>();
    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customer, universeUUID);
    for (List<CustomerTaskFormData> task : taskList.values()) {
      flattenList.addAll(task);
    }
    return PlatformResults.withData(flattenList);
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result universeTasks(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    Map<UUID, List<CustomerTaskFormData>> taskList =
        fetchTasks(customer, universe.getUniverseUUID());
    return PlatformResults.withData(taskList);
  }

  @ApiOperation(value = "Get a task's status", response = Object.class)
  public Result taskStatus(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    ObjectNode responseJson = commissioner.getStatusOrBadRequest(taskUUID);
    return ok(responseJson);
  }

  @ApiOperation(
      value =
          "Deprecated: sinceDate=2023-06-06, sinceYBAVersion=2.19.1.0, "
              + "Use /api/v1/customers/{cUUID}/tasks/{tUUID}/failed_subtasks instead",
      responseContainer = "Map",
      response = Object.class)
  @Deprecated
  public Result failedSubtasks(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    List<SubTaskFormData> failedSubTasks = fetchFailedSubTasks(taskUUID);
    ObjectNode responseJson = Json.newObject();
    responseJson.put("failedSubTasks", Json.toJson(failedSubTasks));
    return ok(responseJson);
  }

  @ApiOperation(value = "Get a list of task's failed subtasks", response = FailedSubtasks.class)
  public Result listFailedSubtasks(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    FailedSubtasks failedSubtasks = new FailedSubtasks();
    List<SubTaskFormData> failedSubtaskFormDataList = fetchFailedSubTasks(taskUUID);
    failedSubtasks.failedSubTasks =
        failedSubtaskFormDataList.stream()
            .map(s -> FailedSubtasks.toSubtaskData(s))
            .collect(Collectors.toList());
    return PlatformResults.withData(failedSubtasks);
  }

  @ApiOperation(
      value = "Retry a Universe or Provider task",
      notes = "Retry a Universe or Provider task.",
      response = PlatformResults.YBPTask.class)
  public Result retryTask(UUID customerUUID, UUID taskUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CustomerTask customerTask = CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    JsonNode oldTaskParams = commissioner.getTaskDetails(taskUUID);
    TaskType taskType = taskInfo.getTaskType();
    LOG.info(
        "Will retry task {}, of type {} in {} state.", taskUUID, taskType, taskInfo.getTaskState());
    if (!commissioner.isTaskRetryable(taskType)) {
      String errMsg = String.format("Invalid task type: Task %s cannot be retried", taskUUID);
      return ApiResponse.error(BAD_REQUEST, errMsg);
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
        UniverseDefinitionTaskParams.Cluster nodeCluster = Universe.getCluster(universe, nodeName);
        nodeTaskParams.upsertCluster(
            nodeCluster.userIntent, nodeCluster.placementInfo, nodeCluster.uuid);

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
        backupTableParams.customerUuid = customerUUID;
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
        multiTableParams.customerUUID = customerUUID;
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
        return ApiResponse.error(BAD_REQUEST, errMsg);
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
        return ApiResponse.error(BAD_REQUEST, errMsg);
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

    CustomerTask.create(
        customer,
        targetUUID,
        newTaskUUID,
        customerTask.getTargetType(),
        customerTask.getType(),
        targetName);
    LOG.info(
        "Saved task uuid {} in customer tasks table for target {}:{}",
        newTaskUUID,
        targetUUID,
        targetName);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomerTask,
            taskUUID.toString(),
            Audit.ActionType.Retry,
            Json.toJson(taskParams),
            newTaskUUID);

    return new PlatformResults.YBPTask(newTaskUUID, targetUUID).asResult();
  }

  @ApiOperation(
      value = "Abort a task",
      notes = "Aborts a running task",
      response = YBPSuccess.class)
  public Result abortTask(UUID customerUUID, UUID taskUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    // Validate if task belongs to the user or not
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    boolean isSuccess = commissioner.abortTask(taskUUID);
    if (!isSuccess) {
      return YBPSuccess.withMessage("Task is not running.");
    }
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.CustomerTask, taskUUID.toString(), Audit.ActionType.Abort);
    return YBPSuccess.withMessage("Task is being aborted.");
  }

  @ApiOperation(
      hidden = true,
      value = "Resume a paused task",
      notes = "Resumes a paused task",
      response = YBPSuccess.class)
  // Hidden API for internal consumption.
  public Result resumeTask(UUID customerUUID, UUID taskUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    // Validate if task belongs to the user or not
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    boolean isSuccess = commissioner.resumeTask(taskUUID);
    if (!isSuccess) {
      return YBPSuccess.withMessage("Task is not paused.");
    }
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.CustomerTask, taskUUID.toString(), Audit.ActionType.Resume);
    return YBPSuccess.withMessage("Task is resumed.");
  }
}
