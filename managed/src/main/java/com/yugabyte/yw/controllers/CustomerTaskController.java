// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.SubTaskFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Query;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Customer Tasks",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerTaskController extends AuthenticatedController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private Commissioner commissioner;

  static final String CUSTOMER_TASK_DB_QUERY_LIMIT = "yb.customer_task_db_query_limit";
  private static final String YB_SOFTWARE_VERSION = "ybSoftwareVersion";
  private static final String YB_PREV_SOFTWARE_VERSION = "ybPrevSoftwareVersion";

  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  private List<SubTaskFormData> fetchFailedSubTasks(UUID parentUUID) {
    TaskInfo parentTask = TaskInfo.getOrBadRequest(parentUUID);
    Query<TaskInfo> subTaskQuery =
        TaskInfo.find
            .query()
            .where()
            .eq("parent_uuid", parentUUID)
            .in("task_state", TaskInfo.ERROR_STATES)
            .orderBy("position desc");
    LinkedList<TaskInfo> result = new LinkedList<>(subTaskQuery.findList());

    if (TaskInfo.ERROR_STATES.contains(parentTask.getTaskState()) && result.isEmpty()) {
      JsonNode taskError = parentTask.getTaskDetails().get("errorString");
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
      subTaskData.creationTime = taskInfo.getCreationTime();
      subTaskData.subTaskGroupType = taskInfo.getSubTaskGroupType().name();
      JsonNode taskError = taskInfo.getTaskDetails().get("errorString");
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
      taskData.target = task.getTarget().name();
      taskData.type = task.getType().name();
      taskData.typeName =
          task.getCustomTypeName() != null
              ? task.getCustomTypeName()
              : task.getType().getFriendlyName();
      taskData.targetUUID = task.getTargetUUID();
      ObjectNode versionNumbers = Json.newObject();
      JsonNode taskDetails = taskInfo.getTaskDetails();
      if (taskData.type == "UpgradeSoftware" && taskDetails.has(YB_PREV_SOFTWARE_VERSION)) {
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

  private Map<UUID, List<CustomerTaskFormData>> fetchTasks(UUID customerUUID, UUID targetUUID) {
    Query<CustomerTask> customerTaskQuery =
        CustomerTask.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .orderBy("create_time desc");

    if (targetUUID != null) {
      customerTaskQuery.where().eq("target_uuid", targetUUID);
    }

    List<CustomerTask> customerTaskList =
        customerTaskQuery
            .setMaxRows(
                runtimeConfigFactory.globalRuntimeConf().getInt(CUSTOMER_TASK_DB_QUERY_LIMIT))
            .orderBy("create_time desc")
            .findPagedList()
            .getList();

    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();

    Set<UUID> taskUuids =
        customerTaskList.stream().map(CustomerTask::getTaskUUID).collect(Collectors.toSet());
    Map<UUID, TaskInfo> taskInfoMap =
        TaskInfo.find(taskUuids)
            .stream()
            .collect(Collectors.toMap(TaskInfo::getTaskUUID, Function.identity()));
    for (CustomerTask task : customerTaskList) {
      Optional<ObjectNode> optTaskProgress =
          commissioner.buildTaskStatus(task, taskInfoMap.get(task.getTaskUUID()));
      // If the task progress API returns error, we will log it and not add that task
      // to the task list for UI rendering.
      optTaskProgress.ifPresent(
          taskProgress -> {
            if (taskProgress.has("error")) {
              LOG.error(
                  "Error fetching task progress for {}. Error: {}",
                  task.getTaskUUID(),
                  taskProgress.get("error"));
            } else {
              CustomerTaskFormData taskData =
                  buildCustomerTaskFromData(
                      task, taskProgress, taskInfoMap.get(task.getTaskUUID()));
              if (taskData != null) {
                List<CustomerTaskFormData> taskList =
                    taskListMap.getOrDefault(task.getTargetUUID(), new ArrayList<>());
                taskList.add(taskData);
                taskListMap.putIfAbsent(task.getTargetUUID(), taskList);
              }
            }
          });
    }
    return taskListMap;
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customerUUID, null);
    return PlatformResults.withData(taskList);
  }

  @ApiOperation(
      value = "List task",
      response = CustomerTaskFormData.class,
      responseContainer = "List")
  public Result tasksList(UUID customerUUID, UUID universeUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<CustomerTaskFormData> flattenList = new ArrayList<CustomerTaskFormData>();
    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customerUUID, universeUUID);
    for (List<CustomerTaskFormData> task : taskList.values()) {
      flattenList.addAll(task);
    }
    return PlatformResults.withData(flattenList);
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result universeTasks(UUID customerUUID, UUID universeUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Map<UUID, List<CustomerTaskFormData>> taskList =
        fetchTasks(customerUUID, universe.universeUUID);
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
      value = "Get a task's failed subtasks",
      responseContainer = "Map",
      response = Object.class)
  public Result failedSubtasks(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    List<SubTaskFormData> failedSubTasks = fetchFailedSubTasks(taskUUID);
    ObjectNode responseJson = Json.newObject();
    responseJson.put("failedSubTasks", Json.toJson(failedSubTasks));
    return ok(responseJson);
  }

  @ApiOperation(
      value = "Retry a Universe task",
      notes = "Retry a Universe task.",
      response = UniverseResp.class)
  public Result retryTask(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CustomerTask customerTask = CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    JsonNode oldTaskParams = commissioner.getTaskDetails(taskUUID);
    TaskType taskType = taskInfo.getTaskType();
    LOG.info(
        "Will retry task {}, of type {} in {} state.", taskUUID, taskType, taskInfo.getTaskState());
    if (!Commissioner.isTaskRetryable(taskType)) {
      String errMsg = String.format("Invalid task type: Task %s cannot be retried", taskUUID);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    UniverseTaskParams taskParams = null;
    switch (taskType) {
      case CreateUniverse:
      case EditUniverse:
      case ReadOnlyClusterCreate:
        UniverseDefinitionTaskParams params =
            Json.fromJson(oldTaskParams, UniverseDefinitionTaskParams.class);
        // Reset the error string.
        params.setErrorString(null);
        taskParams = params;
        break;
      case RemoveNodeFromUniverse:
      case DeleteNodeFromUniverse:
      case ReleaseInstanceFromUniverse:
        String nodeName = oldTaskParams.get("nodeName").textValue();
        String universeUUIDStr = oldTaskParams.get("universeUUID").textValue();
        UUID universeUUID = UUID.fromString(universeUUIDStr);
        int expectedUniverseVersion = oldTaskParams.get("expectedUniverseVersion").asInt();
        // Build node task params for node actions.
        NodeTaskParams nodeTaskParams = new NodeTaskParams();
        nodeTaskParams.nodeName = nodeName;
        nodeTaskParams.universeUUID = universeUUID;
        nodeTaskParams.expectedUniverseVersion = -1;
        taskParams = nodeTaskParams;
        break;
      default:
        String errMsg =
            String.format(
                "Invalid task type: %s. Only Universe, some Node task retries are supported.",
                taskType);
        return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);

    if (!taskUUID.equals(universe.getUniverseDetails().updatingTaskUUID)) {
      String errMsg = String.format("Invalid task state: Task %s cannot be retried", taskUUID);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }
    taskParams.firstTry = false;
    taskParams.setPreviousTaskUUID(taskUUID);
    UUID newTaskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted retry task to universe for {}:{}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        newTaskUUID);

    CustomerTask.create(
        customer,
        universe.universeUUID,
        newTaskUUID,
        customerTask.getTarget(),
        customerTask.getType(),
        universe.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}",
        newTaskUUID,
        universe.universeUUID,
        universe.name);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CustomerTask,
            taskUUID.toString(),
            Audit.ActionType.Retry,
            Json.toJson(taskParams),
            newTaskUUID);

    return PlatformResults.withData(new UniverseResp(universe, newTaskUUID));
  }

  @ApiOperation(
      value = "Abort a task",
      notes = "Aborts a running task",
      response = YBPSuccess.class)
  public Result abortTask(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    boolean isSuccess = commissioner.abortTask(taskUUID);
    if (!isSuccess) {
      return YBPSuccess.withMessage("Task is not running.");
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.CustomerTask, taskUUID.toString(), Audit.ActionType.Abort);
    return YBPSuccess.withMessage("Task is being aborted.");
  }

  @ApiOperation(
      hidden = true,
      value = "Resume a paused task",
      notes = "Resumes a paused task",
      response = YBPSuccess.class)
  // Hidden API for internal consumption.
  public Result resumeTask(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    boolean isSuccess = commissioner.resumeTask(taskUUID);
    if (!isSuccess) {
      return YBPSuccess.withMessage("Task is not paused.");
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.CustomerTask, taskUUID.toString(), Audit.ActionType.Resume);
    return YBPSuccess.withMessage("Task is resumed.");
  }
}
