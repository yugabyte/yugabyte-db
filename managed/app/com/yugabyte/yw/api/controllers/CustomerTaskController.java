// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.api.forms.CustomerTaskFormData;
import com.yugabyte.yw.api.models.Customer;
import com.yugabyte.yw.api.models.CustomerTask;
import com.yugabyte.yw.commissioner.controllers.TasksController;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.controllers.AuthenticatedController;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import com.yugabyte.yw.ui.views.html.*;

import java.util.*;

public class CustomerTaskController extends AuthenticatedController {
  @Inject
  ApiHelper apiHelper;

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
    String commissionerBaseUrl = "http://" + ctx().request().host() + "/commissioner/tasks/";
    for (CustomerTask task: pendingTasks) {
      CustomerTaskFormData taskData = new CustomerTaskFormData();
      JsonNode taskProgress =
        apiHelper.getRequest(commissionerBaseUrl + task.getTaskUUID());

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
}
