// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.EditBackupScheduleParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.filters.ScheduleApiFilter;
import com.yugabyte.yw.forms.paging.SchedulePagedApiQuery;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.filters.ScheduleFilter;
import com.yugabyte.yw.models.paging.SchedulePagedApiResponse;
import com.yugabyte.yw.models.paging.SchedulePagedQuery;
import com.yugabyte.yw.models.paging.SchedulePagedResponse;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.Model;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Schedule management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ScheduleController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleController.class);

  private final BackupHelper backupHelper;

  @Inject
  public ScheduleController(BackupHelper backupHelper) {
    this.backupHelper = backupHelper;
  }

  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  @ApiOperation(
      value =
          "Deprecated since YBA version 2.20.0.0."
              + " Use 'List schedules V2' instead. List schedules",
      response = Schedule.class,
      responseContainer = "List",
      nickname = "listSchedules")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<Schedule> schedules = Schedule.getAllActiveByCustomerUUID(customerUUID);
    return PlatformResults.withData(schedules);
  }

  @ApiOperation(
      value = "List schedules V2",
      response = SchedulePagedResponse.class,
      nickname = "listSchedulesV2")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageScheduleRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.SchedulePagedApiQuery",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result pageScheduleList(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    SchedulePagedApiQuery apiQuery = parseJsonAndValidate(request, SchedulePagedApiQuery.class);
    ScheduleApiFilter apiFilter = apiQuery.getFilter();
    ScheduleFilter filter = apiFilter.toFilter().toBuilder().customerUUID(customerUUID).build();
    SchedulePagedQuery query = apiQuery.copyWithFilter(filter, SchedulePagedQuery.class);
    SchedulePagedApiResponse schedules = Schedule.pagedList(query);
    return PlatformResults.withData(schedules);
  }

  @ApiOperation(value = "Get Schedule", response = Schedule.class, nickname = "getSchedule")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID customerUUID, UUID scheduleUUID) {
    Customer.getOrBadRequest(customerUUID);

    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    return PlatformResults.withData(schedule);
  }

  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  @ApiOperation(
      value =
          "Deprecated since YBA version 2.20.0.0."
              + " Use 'Delete a schedule V2' instead. Delete a schedule",
      response = PlatformResults.YBPSuccess.class,
      nickname = "deleteSchedule")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID scheduleUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);

    schedule.stopSchedule();

    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Schedule, scheduleUUID.toString(), Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Edit a backup schedule V2",
      response = Schedule.class,
      nickname = "editBackupScheduleV2")
  @ApiImplicitParams({
    @ApiImplicitParam(
        required = true,
        dataType = "com.yugabyte.yw.forms.EditBackupScheduleParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation =
            @Resource(
                path = Util.UNIVERSE_UUID,
                sourceType = SourceType.DB,
                dbClass = Schedule.class,
                identifier = "schedules",
                columnName = "schedule_uuid"))
  })
  public Result editBackupSchedule(UUID customerUUID, UUID scheduleUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    EditBackupScheduleParams params = parseJsonAndValidate(request, EditBackupScheduleParams.class);
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
      } else if (schedule.getStatus().equals(State.Active) && schedule.isRunningState()) {
        throw new PlatformServiceException(CONFLICT, "Cannot edit schedule as it is running.");
      } else if (params.frequency != null) {
        if (params.frequencyTimeUnit == null) {
          throw new PlatformServiceException(BAD_REQUEST, "Please provide time unit for frequency");
        }
        BackupUtil.validateBackupFrequency(params.frequency);
        schedule.updateFrequency(params.frequency);
        schedule.updateFrequencyTimeUnit(params.frequencyTimeUnit);

        ScheduleTask lastTask = ScheduleTask.getLastTask(schedule.getScheduleUUID());
        Date nextScheduleTaskTime;

        if (lastTask == null
            || Util.isTimeExpired(
                Schedule.nextExpectedTaskTime(lastTask.getScheduledTime(), schedule))) {
          nextScheduleTaskTime = Schedule.nextExpectedTaskTime(null, schedule);
        } else {
          nextScheduleTaskTime =
              Schedule.nextExpectedTaskTime(lastTask.getScheduledTime(), schedule);
        }

        schedule.updateNextScheduleTaskTime(nextScheduleTaskTime);

      } else if (params.cronExpression != null) {
        BackupUtil.validateBackupCronExpression(params.cronExpression);
        schedule.updateCronExpression(params.cronExpression);
        Date nextScheduleTaskTime = Schedule.nextExpectedTaskTime(null, schedule);
        schedule.updateNextScheduleTaskTime(nextScheduleTaskTime);
      }

      // Update incremental backup schedule frequency, if provided after validation.
      if (params.incrementalBackupFrequency != null) {
        if (ScheduleUtil.isIncrementalBackupSchedule(scheduleUUID)) {
          if (params.incrementalBackupFrequencyTimeUnit == null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Please provide time unit for incremental backup frequency");
          }
          long schedulingFrequency =
              (StringUtils.isEmpty(params.cronExpression))
                  ? params.frequency
                  : BackupUtil.getCronExpressionTimeInterval(params.cronExpression);
          backupHelper.validateIncrementalScheduleFrequency(
              params.incrementalBackupFrequency,
              schedulingFrequency,
              Universe.getOrBadRequest(schedule.getOwnerUUID(), customer));
          schedule.updateIncrementalBackupFrequencyAndTimeUnit(
              params.incrementalBackupFrequency, params.incrementalBackupFrequencyTimeUnit);
          schedule.updateNextIncrementScheduleTaskTime(
              ScheduleUtil.nextExpectedIncrementTaskTime(schedule));
        } else {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Cannot assign incremental backup frequency to a non-incremental schedule");
        }
      }
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Schedule, scheduleUUID.toString(), Audit.ActionType.Edit);
    return PlatformResults.withData(schedule);
  }

  @ApiOperation(
      value = "Delete a schedule V2",
      response = PlatformResults.YBPSuccess.class,
      nickname = "deleteScheduleV2")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteYb(UUID customerUUID, UUID scheduleUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    if (schedule.getStatus().equals(State.Active) && schedule.isRunningState()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot delete schedule as it is running.");
    }
    schedule.stopSchedule();
    ScheduleTask.getAllTasks(scheduleUUID).forEach(Model::delete);
    schedule.delete();
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Schedule, scheduleUUID.toString(), Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }
}
