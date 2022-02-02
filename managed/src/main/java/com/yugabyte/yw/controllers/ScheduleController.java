// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.cronutils.model.Cron;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.EditBackupScheduleParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.ScheduleTask;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import static com.cronutils.model.CronType.UNIX;

@Api(
    value = "Backup schedule management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ScheduleController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleController.class);

  private static final long MIN_SCHEDULE_DURATION_IN_SECS = 3600L;
  private static final long MIN_SCHEDULE_DURATION_IN_MILLIS = MIN_SCHEDULE_DURATION_IN_SECS * 1000L;

  @ApiOperation(
      value = "List backup schedules",
      response = Schedule.class,
      responseContainer = "List",
      nickname = "listBackupSchedules")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<Schedule> schedules = Schedule.getAllActiveByCustomerUUID(customerUUID);
    return PlatformResults.withData(schedules);
  }

  @ApiOperation(
      value = "Delete a backup schedule",
      response = PlatformResults.YBPSuccess.class,
      nickname = "deleteBackupSchedule")
  public Result delete(UUID customerUUID, UUID scheduleUUID) {
    Customer.getOrBadRequest(customerUUID);

    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);

    schedule.stopSchedule();

    ObjectNode responseJson = Json.newObject();
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Edit a backup schedule",
      response = Schedule.class,
      nickname = "editBackupSchedule")
  @ApiImplicitParams({
    @ApiImplicitParam(
        required = true,
        dataType = "com.yugabyte.yw.forms.EditBackupScheduleParams",
        paramType = "body")
  })
  public Result editBackupSchedule(UUID customerUUID, UUID scheduleUUID) {
    Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);

    EditBackupScheduleParams params = parseJsonAndValidate(EditBackupScheduleParams.class);
    if (params.status.equals(State.Paused)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "State paused is an internal state and cannot be specified by the user");
    } else if (params.status.equals(State.Stopped)) {
      schedule.stopSchedule();
    } else if (params.status.equals(State.Active)) {
      if (params.frequency == null && params.cronExpression == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Both schedule frequency and cron expression cannot be null");
      } else if (params.frequency != null && params.cronExpression != null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Both schedule frequency and cron expression cannot be provided");
      } else if (schedule.getStatus().equals(State.Active) && schedule.getRunningState()) {
        throw new PlatformServiceException(CONFLICT, "Cannot edit schedule as it is running.");
      } else if (params.frequency != null) {
        if (params.frequency < MIN_SCHEDULE_DURATION_IN_MILLIS) {
          throw new PlatformServiceException(BAD_REQUEST, "Min schedule duration is 1 hour");
        } else {
          schedule.updateFrequency(params.frequency);
        }
      } else if (params.cronExpression != null) {
        Cron parsedUnixCronExpression;
        try {
          CronParser unixCronParser =
              new CronParser(CronDefinitionBuilder.instanceDefinitionFor(UNIX));
          parsedUnixCronExpression = unixCronParser.parse(params.cronExpression);
          parsedUnixCronExpression.validate();
        } catch (Exception ex) {
          throw new PlatformServiceException(BAD_REQUEST, "Cron expression specified is invalid");
        }
        ExecutionTime executionTime = ExecutionTime.forCron(parsedUnixCronExpression);
        Duration timeToNextExecution =
            executionTime.timeToNextExecution(Instant.now().atZone(ZoneId.of("UTC"))).get();
        Duration timeFromLastExecution =
            executionTime.timeFromLastExecution(Instant.now().atZone(ZoneId.of("UTC"))).get();
        Duration duration = Duration.ZERO;
        duration = duration.plus(timeToNextExecution).plus(timeFromLastExecution);
        if (duration.getSeconds() < MIN_SCHEDULE_DURATION_IN_SECS) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Duration between the cron schedules cannot be less than 1 hour");
        }
        schedule.updateCronExpression(params.cronExpression);
      }
    }
    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(schedule);
  }

  @ApiOperation(
      value = "Delete a schedule V2",
      response = PlatformResults.YBPSuccess.class,
      nickname = "deleteScheduleV2")
  public Result deleteYb(UUID customerUUID, UUID scheduleUUID) {
    Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);
    if (schedule.getStatus().equals(State.Active) && schedule.getRunningState()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot delete schedule as it is running.");
    }
    schedule.stopSchedule();
    ScheduleTask.getAllTasks(scheduleUUID).forEach((scheduleTask) -> scheduleTask.delete());
    schedule.delete();
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }
}
