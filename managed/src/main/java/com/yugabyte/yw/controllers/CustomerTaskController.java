// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Strings;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.SubTaskFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.FailedSubtasks;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.ExpressionList;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Customer Tasks",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerTaskController extends AuthenticatedController {

  @Inject private RuntimeConfGetter confGetter;
  @Inject private Commissioner commissioner;
  @Inject private CustomerTaskManager customerTaskManager;

  static final String CUSTOMER_TASK_DB_QUERY_LIMIT = "yb.customer_task_db_query_limit";

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
      if (taskProgress.has("details")) {
        taskData.details = taskProgress.get("details");
      } else {
        ObjectNode details = Json.newObject();
        // Add auditLogConfig from the task details if it is present.
        // This info is useful to render the UI properly while task is in progress.
        if (taskInfo.getDetails().has("auditLogConfig")) {
          details.set("auditLogConfig", taskInfo.getDetails().get("auditLogConfig"));
        }
        ObjectNode versionNumbers = commissioner.getVersionInfo(task, taskInfo);
        if (versionNumbers != null && !versionNumbers.isEmpty()) {
          details.set("versionNumbers", versionNumbers);
          taskData.details = details;
        }
      }
      String correlationId = task.getCorrelationId();
      if (!Strings.isNullOrEmpty(correlationId)) taskData.correlationId = correlationId;
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customer, null);
    return PlatformResults.withData(taskList);
  }

  @ApiOperation(
      value = "List task",
      response = CustomerTaskFormData.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result universeTasks(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    Map<UUID, List<CustomerTaskFormData>> taskList =
        fetchTasks(customer, universe.getUniverseUUID());
    return PlatformResults.withData(taskList);
  }

  @ApiOperation(value = "Get a task's status", response = Object.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result taskStatus(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    ObjectNode responseJson = commissioner.getStatusOrBadRequest(taskUUID);
    return ok(responseJson);
  }

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.19.1.0")
  @ApiOperation(
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.19.1.0.</b></p>"
              + "Use /api/v1/customers/{cUUID}/tasks/{tUUID}/failed_subtasks instead.",
      value = "Fetch failed subtasks - deprecated",
      responseContainer = "Map",
      response = Object.class)
  @Deprecated
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result failedSubtasks(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    List<SubTaskFormData> failedSubTasks = fetchFailedSubTasks(taskUUID);
    ObjectNode responseJson = Json.newObject();
    responseJson.put("failedSubTasks", Json.toJson(failedSubTasks));
    return ok(responseJson);
  }

  @ApiOperation(value = "Get a list of task's failed subtasks", response = FailedSubtasks.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation =
            @Resource(
                path = "details.universeUUID",
                sourceType = SourceType.DB,
                dbClass = TaskInfo.class,
                identifier = "tasks",
                columnName = "uuid"))
  })
  public Result retryTask(UUID customerUUID, UUID taskUUID, Http.Request request) {
    CustomerTask customerTask = customerTaskManager.retryCustomerTask(customerUUID, taskUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    LOG.info(
        "Saved task uuid {} in customer tasks table for target {}:{}",
        customerTask.getTaskUUID(),
        customerTask.getTargetUUID(),
        customerTask.getTargetName());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomerTask,
            taskUUID.toString(),
            Audit.ActionType.Retry,
            Json.toJson(taskInfo),
            customerTask.getTaskUUID());

    return new PlatformResults.YBPTask(customerTask.getTaskUUID(), customerTask.getTargetUUID())
        .asResult();
  }

  @ApiOperation(
      value = "Abort a task",
      notes = "Aborts a running task",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation =
            @Resource(
                path = "details.universeUUID",
                sourceType = SourceType.DB,
                dbClass = TaskInfo.class,
                identifier = "tasks",
                columnName = "uuid"))
  })
  public Result abortTask(UUID customerUUID, UUID taskUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    // Validate if task belongs to the user or not
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);
    boolean isSuccess = commissioner.abortTask(taskUUID, false);
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
