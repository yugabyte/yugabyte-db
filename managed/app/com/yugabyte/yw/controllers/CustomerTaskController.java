// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;
import java.util.Set;
import java.util.ArrayList;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import com.yugabyte.yw.ui.controllers.AuthenticatedController;
import com.yugabyte.yw.ui.views.html.*;


import static com.yugabyte.yw.ui.controllers.TokenAuthenticator.COOKIE_AUTH_TOKEN;

public class CustomerTaskController extends AuthenticatedController {
  @Inject
  ApiHelper apiHelper;

  @Inject
  Commissioner commissioner;

  protected static final int TASK_HISTORY_LIMIT = 6;
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  public Result list(UUID customerUUID) {
    Customer customer = Customer.find.byId(customerUUID);

    if (customer == null) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("error", "Invalid Customer UUID: " + customerUUID);
      return badRequest(responseJson);
    }

    Set<CustomerTask> pendingTasks = CustomerTask.find.where()
      .eq("customer_uuid", customer.uuid)
      .orderBy("create_time desc")
      .setMaxRows(TASK_HISTORY_LIMIT)
      .findSet();

    List<CustomerTaskFormData> taskList = new ArrayList<>();
    String customerTaskBaseUrl =
      "http://" + ctx().request().host() + "/api/customers/" + customer.uuid + "/tasks/";
    for (CustomerTask task : pendingTasks) {
      CustomerTaskFormData taskData = new CustomerTaskFormData();

      JsonNode taskProgress = apiHelper.getRequest(customerTaskBaseUrl + task.getTaskUUID(),
        ImmutableMap.of("X-AUTH-TOKEN", ctx().request().cookie(COOKIE_AUTH_TOKEN).value()));

      // If the task progress API returns error, we will log it and not add that task to
      // to the task list for UI rendering.
      if (taskProgress.has("error")) {
        LOG.error("Error fetching Task Progress for " + task.getTaskUUID());
      } else {
        taskData.percentComplete = taskProgress.get("percent").asInt();
        if (taskData.percentComplete == 100) {
          task.markAsCompleted();
        }
        taskData.success = taskProgress.get("status").asText().equals("Success") ? true : false;
        taskData.id = task.getTaskUUID();
        taskData.title = task.getFriendlyDescription();
        taskList.add(taskData);
      }
    }

    if (request().accepts(Http.MimeTypes.HTML)) {
      return ok(customerTask.render(taskList));
    } else if (request().accepts(Http.MimeTypes.JSON)) {
      return ok(Json.toJson(taskList));
    } else {
      return badRequest();
    }
  }

  public Result status(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.find.byId(customerUUID);

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
