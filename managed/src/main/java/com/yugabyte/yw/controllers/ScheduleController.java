// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

@Api(value = "Schedule", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ScheduleController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleController.class);

  @ApiOperation(value = "list", response = Schedule.class, responseContainer = "List")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<Schedule> schedules = Schedule.getAllActiveByCustomerUUID(customerUUID);
    return YWResults.withData(schedules);
  }

  @ApiOperation(value = "delete", response = YWResults.class)
  public Result delete(UUID customerUUID, UUID scheduleUUID) {
    Customer.getOrBadRequest(customerUUID);

    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);

    schedule.stopSchedule();

    ObjectNode responseJson = Json.newObject();
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }
}
