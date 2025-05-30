// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ScheduleTaskHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.EditBackupScheduleParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleEditParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleToggleParams;
import com.yugabyte.yw.forms.filters.ScheduleApiFilter;
import com.yugabyte.yw.forms.paging.SchedulePagedApiQuery;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.filters.ScheduleFilter;
import com.yugabyte.yw.models.helpers.TaskType;
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
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Schedule management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ScheduleController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleController.class);

  private final BackupHelper backupHelper;
  private final Commissioner commissioner;
  private final ScheduleTaskHelper scheduleTaskHelper;
  private final RuntimeConfGetter confGetter;

  @Inject
  public ScheduleController(
      BackupHelper backupHelper,
      Commissioner commissioner,
      ScheduleTaskHelper scheduleTaskHelper,
      RuntimeConfGetter confGetter) {
    this.backupHelper = backupHelper;
    this.commissioner = commissioner;
    this.scheduleTaskHelper = scheduleTaskHelper;
    this.confGetter = confGetter;
  }

  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  @ApiOperation(
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.0.</b></p>"
              + " Use 'List schedules V2' instead.",
      value = "List schedules - deprecated",
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
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.0.</b></p>"
              + "Use 'Delete a schedule V2' instead.",
      value = "Delete a schedule  - deprecated",
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

  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.0.0")
  @ApiOperation(
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b></p>"
              + "Use 'Edit a backup schedule async' instead.",
      value = "Edit a backup schedule V2 - deprecated",
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
    if (params.status == null) {
      params.status = State.Active;
    }
    // Check this API is not used to modify PIT enabled schedule.
    ScheduleUtil.checkScheduleActionFromDeprecatedMethod(schedule);
    if (params.status.equals(State.Paused)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "State paused is an internal state and cannot be specified by the user");
    } else if (params.status.equals(State.Stopped)) {
      Schedule.updateStatusAndSave(customerUUID, scheduleUUID, State.Stopped);
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

        if (schedule.getNextScheduleTaskTime() != null
            && Util.isTimeExpired(schedule.getNextScheduleTaskTime())
            && params.runImmediateBackupOnResume
            && schedule.getTaskType() == TaskType.CreateBackup) {
          LOG.info("Schedule {} will run immediately because of backlog", scheduleUUID);
          schedule.updateBacklogStatus(true);
        }

        if (schedule.getNextScheduleTaskTime() == null) {
          LOG.info("No next time found for schedule {}, run next task immediately", scheduleUUID);
          schedule.updateNextScheduleTaskTime(schedule.nextExpectedTaskTime(null));
        } else if (Util.isTimeExpired(schedule.getNextScheduleTaskTime())) {
          LOG.debug("Schedule {} is expired, calculate next run", schedule.getScheduleUUID());
          schedule.updateNextScheduleTaskTime(
              schedule.nextExpectedTaskTime(schedule.getNextScheduleTaskTime()));
        } else {
          LOG.debug(
              "Schedule {} is not expired, next run is {}",
              schedule.getScheduleUUID(),
              schedule.getNextScheduleTaskTime());
        }
      } else if (params.cronExpression != null) {
        BackupUtil.validateBackupCronExpression(params.cronExpression);
        schedule.updateCronExpression(params.cronExpression);
        Date nextScheduleTaskTime = schedule.nextExpectedTaskTime(null);
        schedule.updateNextScheduleTaskTime(nextScheduleTaskTime);
      }

      // Update retention period if provided.
      if (params.timeBeforeDelete > 0L) {
        schedule.updateBackupRetentionPeriod(params.timeBeforeDelete);
        // Update expiry time of backups related to this schedule.
        Backup.fetchAllCompletedBackupsByScheduleUUID(customerUUID, scheduleUUID)
            .forEach(
                backup -> {
                  Date newExpiry =
                      new DateTime(backup.getCreateTime())
                          .plusMillis((int) params.timeBeforeDelete)
                          .toDate();
                  backup.setExpiry(newExpiry);
                  backup.save();
                });
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
          if (schedule.getNextIncrementScheduleTaskTime() != null
              && Util.isTimeExpired(schedule.getNextIncrementScheduleTaskTime())
              && params.runImmediateBackupOnResume) {
            LOG.info(
                "Incremental chedule {} will run immediately because of backlog", scheduleUUID);
            schedule.updateIncrementBacklogStatus(true);
          }
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

  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.0.0")
  @ApiOperation(
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b></p>"
              + "Use 'Delete a backup schedule async' instead.",
      value = "Delete a schedule V2 - deprecated",
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
    // Check this API is not used to delete PIT enabled schedule
    ScheduleUtil.checkScheduleActionFromDeprecatedMethod(schedule);

    schedule.stopSchedule();
    ScheduleTask.getAllTasks(scheduleUUID).forEach(Model::delete);
    schedule.delete();
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Schedule, scheduleUUID.toString(), Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change. Edit a backup schedule async.",
      value = "Edit a backup schedule async",
      response = Schedule.class,
      nickname = "editBackupScheduleAsync")
  @ApiImplicitParams({
    @ApiImplicitParam(
        required = true,
        dataType = "com.yugabyte.yw.forms.backuprestore.BackupScheduleEditParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Result editBackupScheduleAsync(
      UUID customerUUID, UUID universeUUID, UUID scheduleUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    if (!schedule.getOwnerUUID().equals(universeUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Schedule not owned by Universe.");
    }
    BackupScheduleEditParams requestParams =
        parseJsonAndValidate(request, BackupScheduleEditParams.class);
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    scheduleParams.applyScheduleEditParams(requestParams);
    // Check if attempting to modify schedule when universe is locked( not allowed ).
    Universe universe = Universe.getOrBadRequest(scheduleParams.getUniverseUUID());
    if (schedule.isRunningState()
        || (scheduleParams.enablePointInTimeRestore && universe.universeIsLocked())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit schedule as Universe is locked.");
    }

    // Generate task params with modified schedule params
    BackupScheduleTaskParams taskParams =
        UniverseControllerRequestBinder.bindFormDataToUpgradeTaskParams(
            request, BackupScheduleTaskParams.class, universe);
    taskParams.setCustomerUUID(customerUUID);
    taskParams.setScheduleUUID(schedule.getScheduleUUID());
    taskParams.setScheduleParams(scheduleParams);

    UUID taskUUID =
        scheduleTaskHelper.createEditScheduledBackupTask(taskParams, customer, universe, schedule);
    auditService().createAuditEntry(request, Json.toJson(taskParams), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change. Delete a backup schedule async.",
      value = "Delete a backup schedule async",
      response = Schedule.class,
      nickname = "deleteBackupScheduleAsync")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Result deleteBackupScheduleAsync(
      UUID customerUUID, UUID universeUUID, UUID scheduleUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    UUID taskUUID =
        scheduleTaskHelper.createDeleteScheduledBackupTask(schedule, universeUUID, customer);
    auditService().createAuditEntry(request, taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. Toggle a backup schedule. Only allowed"
              + " to toggle backup schedule state between Active and Stopped.",
      value = "Toggle a backup schedule",
      response = Schedule.class,
      nickname = "toggleBackupSchedule")
  @ApiImplicitParams({
    @ApiImplicitParam(
        required = true,
        dataType = "com.yugabyte.yw.forms.backuprestore.BackupScheduleToggleParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Result toggleSchedule(
      UUID customerUUID, UUID universeUUID, UUID scheduleUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Schedule schedule = Schedule.getOrBadRequest(customerUUID, scheduleUUID);
    if (!schedule.getOwnerUUID().equals(universeUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Schedule not owned by Universe.");
    }

    BackupScheduleToggleParams scheduleToggleParams =
        parseJsonAndValidate(request, BackupScheduleToggleParams.class);
    // Check if attempting to modify schedule when universe is locked( not allowed ).
    // Only allow to toggle schedule between Active and Stopped.
    scheduleToggleParams.verifyScheduleToggle(schedule.getStatus());
    Schedule.toggleBackupSchedule(
        customerUUID,
        scheduleUUID,
        scheduleToggleParams.status,
        scheduleToggleParams.runImmediateBackupOnResume);

    Audit.ActionType actionType =
        scheduleToggleParams.status == Schedule.State.Stopped
            ? Audit.ActionType.StopPeriodicBackup
            : Audit.ActionType.StartPeriodicBackup;
    auditService()
        .createAuditEntry(request, Audit.TargetType.Schedule, scheduleUUID.toString(), actionType);
    return YBPSuccess.empty();
  }
}
