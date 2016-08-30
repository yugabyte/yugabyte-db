// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.ui.controllers.AuthenticatedController;

import play.mvc.Result;

public class CustomerTaskController extends AuthenticatedController {
  @Inject
  ApiHelper apiHelper;

  @Inject
  Commissioner commissioner;

  protected static final int TASK_HISTORY_LIMIT = 6;
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  public Result list(UUID customerUUID) {
    return badRequest();
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
