// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.Map;
import java.util.HashMap;

import com.avaje.ebean.Query;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;

import play.libs.Json;
import play.mvc.Result;

public class CustomerTaskController extends AuthenticatedController {

  @Inject
  Commissioner commissioner;

  protected static final int TASK_HISTORY_LIMIT = 6;
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  public Result list(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("error", "Invalid Customer UUID: " + customerUUID);
      return badRequest(responseJson);
    }

    Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customerUUID, null);
    return ApiResponse.success(taskList);
  }

  public Result universeTasks(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Universe universe = Universe.get(universeUUID);
      Map<UUID, List<CustomerTaskFormData>> taskList = fetchTasks(customerUUID, universe.universeUUID);
      return ApiResponse.success(taskList);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Universe UUID: " + universeUUID);
    }
  }

  private Map<UUID, List<CustomerTaskFormData>> fetchTasks(UUID customerUUID, UUID universeUUID) {
    Query<CustomerTask> taskQuery = CustomerTask.find.where()
      .eq("customer_uuid", customerUUID)
      .orderBy("create_time desc")
      .setMaxRows(TASK_HISTORY_LIMIT);

    if (universeUUID != null) {
      taskQuery.where().eq("universe_uuid", universeUUID);
    }

    Set<CustomerTask> pendingTasks = taskQuery.findSet();

    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();

    for (CustomerTask task : pendingTasks) {
      CustomerTaskFormData taskData = new CustomerTaskFormData();

      JsonNode taskProgress = commissioner.getStatus(task.getTaskUUID());
      // If the task progress API returns error, we will log it and not add that task to
      // to the task list for UI rendering.
      if (taskProgress.has("error")) {
        LOG.error("Error fetching Task Progress for " + task.getTaskUUID() + ", Error: " + taskProgress.get("error"));
      } else {
        taskData.percentComplete = taskProgress.get("percent").asInt();
        if (taskData.percentComplete == 100) {
          task.markAsCompleted();
        }
        taskData.success = taskProgress.get("status").asText().equals("Success") ? true : false;
        taskData.id = task.getTaskUUID();
        taskData.title = task.getFriendlyDescription();
        taskData.createTime = task.getCreateTime();
        taskData.completionTime = task.getCompletionTime();
        taskData.target = task.getTarget().name();
        taskData.type = task.getType().name();

        List<CustomerTaskFormData> taskList = taskListMap.getOrDefault(task.getUniverseUUID(), new ArrayList<>());
        taskList.add(taskData);
        taskListMap.put(task.getUniverseUUID(), taskList);
      }
    }
    return taskListMap;
  }

  public Result status(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    CustomerTask customerTask = CustomerTask.find.where()
      .eq("customer_uuid", customer.uuid)
      .eq("task_uuid", taskUUID)
      .findUnique();

    if (customerTask == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer Task UUID: " + taskUUID);
    }

    try {
      ObjectNode responseJson = commissioner.getStatus(taskUUID);
      return ok(responseJson);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    }
  }
}
