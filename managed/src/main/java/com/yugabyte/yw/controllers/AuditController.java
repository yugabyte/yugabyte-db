// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class AuditController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AuditController.class);

  /**
   * GET endpoint for listing all audit entries for a user.
   *
   * @return JSON response with audit entries belonging to the user.
   */
  public Result list(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    List<Audit> auditList = auditService().getAllUserEntries(user.uuid);
    return YWResults.withData(auditList);
  }

  /**
   * GET endpoint for getting the user associated with a task.
   *
   * @return JSON response with the corresponding audit entry.
   */
  public Result getTaskAudit(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    Audit entry = auditService().getOrBadRequest(customerUUID, taskUUID);
    return YWResults.withData(entry);
  }

  public Result getUserFromTask(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    Audit entry = auditService().getOrBadRequest(customerUUID, taskUUID);
    Users user = Users.get(entry.getUserUUID());
    return YWResults.withData(user);
  }
}
