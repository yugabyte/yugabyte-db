// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class ScheduleController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleController.class);

  public Result list(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      String errMsg = "Invalid Customer UUID: " + customerUUID;
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    List<Schedule> schedules = Schedule.getAllActiveByCustomerUUID(customerUUID);
    return ApiResponse.success(schedules);
  }

  public Result delete(UUID customerUUID, UUID scheduleUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      String errMsg = "Invalid Customer UUID: " + customerUUID;
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    Schedule schedule = Schedule.get(scheduleUUID);
    if (schedule == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Schedule UUID: " + scheduleUUID);
    }

    try {
      schedule.stopSchedule();
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete Schedule UUID: " + scheduleUUID);
    }

    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    auditService().createAuditEntry(ctx(), request());
    return ApiResponse.success(responseJson);
  }
}
