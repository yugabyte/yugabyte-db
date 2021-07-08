// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Query;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.*;

public class CustomerTaskController extends AuthenticatedController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private Commissioner commissioner;

  static final String CUSTOMER_TASK_DB_QUERY_LIMIT = "yb.customer_task_db_query_limit";

  protected static final int TASK_HISTORY_LIMIT = 6;
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  private List<SubTaskFormData> fetchFailedSubTasks(UUID parentUUID) {
    Query<TaskInfo> subTaskQuery =
        TaskInfo.find
            .query()
            .where()
            .eq("parent_uuid", parentUUID)
            .eq("task_state", TaskInfo.State.Failure.name())
            .orderBy("position desc");
    Set<TaskInfo> result = subTaskQuery.findSet();
    List<SubTaskFormData> subTasks = new ArrayList<>();
    for (TaskInfo taskInfo : result) {
      SubTaskFormData subTaskData = new SubTaskFormData();
      subTaskData.subTaskUUID = taskInfo.getTaskUUID();
      subTaskData.subTaskType = taskInfo.getTaskType().name();
      subTaskData.subTaskState = taskInfo.getTaskState().name();
      subTaskData.creationTime = taskInfo.getCreationTime();
      subTaskData.subTaskGroupType = taskInfo.getSubTaskGroupType().name();
      subTaskData.errorString = taskInfo.getTaskDetails().get("errorString").asText();
      subTasks.add(subTaskData);
    }
    return subTasks;
  }

  private CustomerTaskFormData buildCustomerTaskFromData(
      CustomerTask task, ObjectNode taskProgress) {
    try {
      CustomerTaskFormData taskData = new CustomerTaskFormData();
      taskData.percentComplete = taskProgress.get("percent").asInt();
      taskData.status = taskProgress.get("status").asText();
      taskData.id = task.getTaskUUID();
      taskData.title = task.getFriendlyDescription();
      taskData.createTime = task.getCreateTime();
      taskData.completionTime = task.getCompletionTime();
      taskData.target = task.getTarget().name();
      taskData.type = task.getType().getFriendlyName();
      taskData.targetUUID = task.getTargetUUID();
      return taskData;
    } catch (RuntimeException e) {
      LOG.error(
          "Error fetching Task Progress for "
              + task.getTaskUUID()
              + ", TaskInfo with that taskUUID not found");
      return null;
    }
  }

  private Map<UUID, List<CustomerTaskFormData>> fetchTasks(UUID customerUUID, UUID targetUUID) {
    List<CustomerTask> customerTaskList;

    Query<CustomerTask> customerTaskQuery =
        CustomerTask.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .orderBy("create_time desc");

    if (targetUUID != null) {
      customerTaskQuery.where().eq("target_uuid", targetUUID);
    }

    customerTaskList =
        customerTaskQuery
            .setMaxRows(
                runtimeConfigFactory.globalRuntimeConf().getInt(CUSTOMER_TASK_DB_QUERY_LIMIT))
            .orderBy("create_time desc")
            .findPagedList()
            .getList();

    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();

    for (CustomerTask task : customerTaskList) {
      Optional<ObjectNode> optTaskProgress = commissioner.mayGetStatus(task.getTaskUUID());
      // If the task progress API returns error, we will log it and not add that task
      // to the task list for UI rendering.
      if (optTaskProgress.isPresent()) {
        ObjectNode taskProgress = optTaskProgress.get();
        if (taskProgress.has("error")) {
          LOG.error(
              "Error fetching Task Progress for "
                  + task.getTaskUUID()
                  + ", Error: "
                  + taskProgress.get("error"));
        } else {
          CustomerTaskFormData taskData = buildCustomerTaskFromData(task, taskProgress);
          if (taskData != null) {
            List<CustomerTaskFormData> taskList =
                taskListMap.getOrDefault(task.getTargetUUID(), new ArrayList<>());
            taskList.add(taskData);
            taskListMap.putIfAbsent(task.getTargetUUID(), taskList);
          }
        }
      }
    }
    return taskListMap;
  }

  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customerUUID, null);
    return YWResults.withData(taskList);
  }

  public Result universeTasks(UUID customerUUID, UUID universeUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Map<UUID, List<CustomerTaskFormData>> taskList =
        fetchTasks(customerUUID, universe.universeUUID);
    return YWResults.withData(taskList);
  }

  public Result status(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    ObjectNode responseJson = commissioner.getStatusOrBadRequest(taskUUID);
    return ok(responseJson);
  }

  public Result failedSubtasks(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customerUUID, taskUUID);

    List<SubTaskFormData> failedSubTasks = fetchFailedSubTasks(taskUUID);
    ObjectNode responseJson = Json.newObject();
    responseJson.put("failedSubTasks", Json.toJson(failedSubTasks));
    return ok(responseJson);
  }

  public Result retryTask(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    CustomerTask.getOrBadRequest(customer.uuid, taskUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);

    if (taskInfo.getTaskType() != TaskType.CreateUniverse) {
      String errMsg =
          String.format(
              "Invalid task type: %s. Only 'Create Universe' task retries are supported.",
              taskInfo.getTaskType().toString());
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    JsonNode oldTaskParams = commissioner.getTaskDetails(taskUUID);
    UniverseDefinitionTaskParams params =
        Json.fromJson(oldTaskParams, UniverseDefinitionTaskParams.class);
    params.firstTry = false;
    Universe universe = Universe.getOrBadRequest(params.universeUUID);

    UUID newTaskUUID = commissioner.submit(taskInfo.getTaskType(), params);
    LOG.info(
        "Submitted retry task to create universe for {}:{}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        newTaskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        newTaskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Create,
        universe.name);
    LOG.info(
        "Saved task uuid "
            + newTaskUUID
            + " in customer tasks table for universe "
            + universe.universeUUID
            + ":"
            + universe.name);

    auditService().createAuditEntry(ctx(), request(), Json.toJson(params), newTaskUUID);
    return YWResults.withData(new UniverseResp(universe, newTaskUUID));
  }
}
