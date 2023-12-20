// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Query;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import javax.inject.Inject;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;

@Api(
    value = "Customer Tasks",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerTaskController extends AuthenticatedController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private Commissioner commissioner;
  @Inject private CustomerTaskManager customerTaskManager;

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
    Query<CustomerTask> customerTaskQuery =
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
    Map<UUID, String> updatingTaskByTargetMap =
        commissioner.getUpdatingTaskUUIDsForTargets(customer.getCustomerId());
    Map<UUID, String> placementModificationTaskByTargetMap =
        commissioner.getPlacementModificationTaskUUIDsForTargets(customer.getCustomerId());
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
          .buildTaskStatus(task, taskInfo, allowRetryTasksByTargetMap)
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
    Universe universe = Universe.getOrBadRequest(universeUUID);
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
    CustomerTask customerTask = customerTaskManager.retryCustomerTask(customerUUID, taskUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    Universe universe = Universe.getOrBadRequest(taskUUID);
    LOG.info(
        "Saved task uuid {} in customer tasks table for target {}:{}",
        customerTask.getTaskUUID(),
        customerTask.getTargetUUID(),
        customerTask.getTargetName());
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CustomerTask,
            taskUUID.toString(),
            Audit.ActionType.Retry,
            Json.toJson(taskInfo),
            customerTask.getTaskUUID());
    return PlatformResults.withData(new UniverseResp(universe, customerTask.getTargetUUID()));
  }

  @ApiOperation(
      value = "Abort a task",
      notes = "Aborts a running task",
      response = YBPSuccess.class)
  public Result abortTask(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    // Validate if task belongs to the user or not
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    boolean isSuccess = commissioner.abortTask(taskUUID, false);
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
