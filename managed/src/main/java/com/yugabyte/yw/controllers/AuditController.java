// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import com.yugabyte.yw.common.ApiResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;

import play.mvc.Result;

public class AuditController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AuditController.class);

  /**
   * GET endpoint for listing all audit entries for a user.
   * @return JSON response with audit entries belonging to the user.
   */
  public Result list(UUID customerUUID, UUID userUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    Users user = Users.get(userUUID);
    if (user == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid User UUID: " + customerUUID);
    }

    try {
      List<Audit> auditList = auditService().getAllUserEntries(user.uuid);
      return ApiResponse.success(auditList);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to fetch audit history.");
    }
  }

  /**
   * GET endpoint for getting the user associated with a task.
   * @return JSON response with the corresponding audit entry.
   */
  public Result getTaskAudit(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Audit entry = auditService().getFromTaskUUID(taskUUID);
      if (entry.getCustomerUUID().equals(customerUUID)) {
        return ApiResponse.success(entry);
      }
      else {
        return ApiResponse.error(BAD_REQUEST,
          String.format("Task %s does not belong to customer %s", taskUUID, customerUUID));
      }
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to fetch audit entry.");
    }
  }

  public Result getUserFromTask(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Audit entry = auditService().getFromTaskUUID(taskUUID);
      Users user = Users.get(entry.getUserUUID());
      if (entry.getCustomerUUID().equals(customerUUID)) {
        return ApiResponse.success(user);
      }
      else {
        return ApiResponse.error(BAD_REQUEST,
          String.format("Task %s does not belong to customer %s", taskUUID, customerUUID));
      }
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to fetch user.");
    }
  }
}
