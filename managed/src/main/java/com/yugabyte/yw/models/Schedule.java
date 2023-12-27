// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.cronutils.model.CronType.UNIX;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.models.ScheduleResp.BackupInfo;
import com.yugabyte.yw.models.ScheduleResp.ScheduleRespBuilder;
import com.yugabyte.yw.models.filters.ScheduleFilter;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList.KeyspaceTablesListBuilder;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.SchedulePagedApiResponse;
import com.yugabyte.yw.models.paging.SchedulePagedQuery;
import com.yugabyte.yw.models.paging.SchedulePagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.PersistenceContextScope;
import io.ebean.ExpressionList;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.Table;
import javax.persistence.UniqueConstraint;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.time.DateUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Entity
@Table(
    uniqueConstraints =
        @UniqueConstraint(columnNames = {"schedule_name", "customer_uuid", "owner_uuid"}))
@ApiModel(description = "Backup schedule")
public class Schedule extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Schedule.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

  public enum State {
    @EnumValue("Active")
    Active,

    @EnumValue("Paused")
    Paused,

    @EnumValue("Stopped")
    Stopped,
  }

  public enum SortBy implements PagedQuery.SortByIF {
    taskType("taskType"),
    scheduleUUID("scheduleUUID");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return SortBy.scheduleUUID;
    }
  }

  private static final int MAX_FAIL_COUNT = 3;

  @Id
  @ApiModelProperty(value = "Schedule UUID", accessMode = READ_ONLY)
  public UUID scheduleUUID;

  public UUID getScheduleUUID() {
    return scheduleUUID;
  }

  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID customerUUID;

  public UUID getCustomerUUID() {
    return customerUUID;
  }

  @ApiModelProperty(value = "Number of failed backup attempts", accessMode = READ_ONLY)
  @Column(nullable = false, columnDefinition = "integer default 0")
  private int failureCount;

  public int getFailureCount() {
    return failureCount;
  }

  @ApiModelProperty(value = "Frequency of the schedule, in milli seconds", accessMode = READ_WRITE)
  @Column(nullable = false)
  private long frequency;

  public long getFrequency() {
    return frequency;
  }

  public void setFrequency(long frequency) {
    this.frequency = frequency;
  }

  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  private JsonNode taskParams;

  public JsonNode getTaskParams() {
    return taskParams;
  }

  @ApiModelProperty(value = "Type of task to be scheduled.", accessMode = READ_WRITE)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private TaskType taskType;

  public TaskType getTaskType() {
    return taskType;
  }

  @ApiModelProperty(
      value = "Status of the task. Possible values are _Active_, _Paused_, or _Stopped_.",
      accessMode = READ_ONLY)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private State status = State.Active;

  public State getStatus() {
    return status;
  }

  @Column
  @ApiModelProperty(value = "Cron expression for the schedule")
  private String cronExpression;

  @ApiModelProperty(value = "Name of the schedule", accessMode = READ_ONLY)
  @Column(nullable = false)
  private String scheduleName;

  public String getScheduleName() {
    return scheduleName;
  }

  @ApiModelProperty(value = "Owner UUID for the schedule", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID ownerUUID;

  @ApiModelProperty(value = "Time unit of frequency", accessMode = READ_WRITE)
  private TimeUnit frequencyTimeUnit;

  public TimeUnit getFrequencyTimeUnit() {
    return this.frequencyTimeUnit;
  }

  public void setFrequencyTimeunit(TimeUnit frequencyTimeUnit) {
    this.frequencyTimeUnit = frequencyTimeUnit;
  }

  public String getCronExpression() {
    return cronExpression;
  }

  public void setCronExpression(String cronExpression) {
    this.cronExpression = cronExpression;
  }

  public void setCronExpressionAndTaskParams(String cronExpression, ITaskParams params) {
    this.cronExpression = cronExpression;
    this.taskParams = Json.toJson(params);
    save();
  }

  public void setFailureCount(int count) {
    this.failureCount = count;
    if (count >= MAX_FAIL_COUNT) {
      this.status = State.Paused;
    }
    save();
  }

  public void resetSchedule() {
    this.status = State.Active;
    save();
  }

  public void stopSchedule() {
    this.status = State.Stopped;
    save();
  }

  public void updateFrequency(long frequency) {
    this.frequency = frequency;
    this.cronExpression = null;
    resetSchedule();
  }

  public void updateCronExpression(String cronExpression) {
    this.cronExpression = cronExpression;
    this.frequency = 0L;
    resetSchedule();
  }

  public void updateFrequencyTimeUnit(TimeUnit frequencyTimeUnit) {
    setFrequencyTimeunit(frequencyTimeUnit);
    save();
  }

  @Column(nullable = false)
  @ApiModelProperty(value = "Running state of the schedule")
  private boolean runningState = false;

  public boolean getRunningState() {
    return this.runningState;
  }

  public void setRunningState(boolean state) {
    this.runningState = state;
    save();
  }

  public static final Finder<UUID, Schedule> find = new Finder<UUID, Schedule>(Schedule.class) {};

  public static Schedule create(
      UUID customerUUID,
      UUID ownerUUID,
      ITaskParams params,
      TaskType taskType,
      long frequency,
      String cronExpression,
      TimeUnit frequencyTimeUnit,
      String scheduleName) {
    Schedule schedule = new Schedule();
    schedule.scheduleUUID = UUID.randomUUID();
    schedule.customerUUID = customerUUID;
    schedule.failureCount = 0;
    schedule.taskType = taskType;
    schedule.taskParams = Json.toJson(params);
    schedule.frequency = frequency;
    schedule.status = State.Active;
    schedule.cronExpression = cronExpression;
    schedule.ownerUUID = ownerUUID;
    schedule.frequencyTimeUnit = frequencyTimeUnit;
    schedule.scheduleName =
        scheduleName != null ? scheduleName : "schedule-" + schedule.scheduleUUID;
    schedule.save();
    return schedule;
  }

  public static Schedule create(
      UUID customerUUID,
      ITaskParams params,
      TaskType taskType,
      long frequency,
      String cronExpression,
      TimeUnit frequencyTimeUnit) {
    UUID ownerUUID = customerUUID;
    JsonNode scheduleParams = Json.toJson(params);
    if (scheduleParams.has("universeUUID")) {
      ownerUUID = UUID.fromString(scheduleParams.get("universeUUID").asText());
    }
    return create(
        customerUUID,
        ownerUUID,
        params,
        taskType,
        frequency,
        cronExpression,
        frequencyTimeUnit,
        null);
  }

  public static Schedule create(
      UUID customerUUID,
      ITaskParams params,
      TaskType taskType,
      long frequency,
      String cronExpression) {
    return create(customerUUID, params, taskType, frequency, cronExpression, TimeUnit.MINUTES);
  }

  /** DEPRECATED: use {@link #getOrBadRequest()} */
  @Deprecated
  public static Schedule get(UUID scheduleUUID) {
    return find.query().where().idEq(scheduleUUID).findOne();
  }

  public static Optional<Schedule> maybeGet(UUID scheduleUUID) {
    return Optional.ofNullable(get(scheduleUUID));
  }

  public static Schedule getOrBadRequest(UUID scheduleUUID) {
    Schedule schedule = get(scheduleUUID);
    if (schedule == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Schedule UUID: " + scheduleUUID);
    }
    return schedule;
  }

  public static Schedule getOrBadRequest(UUID customerUUID, UUID scheduleUUID) {
    Schedule schedule =
        find.query().where().idEq(scheduleUUID).eq("customer_uuid", customerUUID).findOne();
    if (schedule == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid Customer UUID: " + customerUUID + ", Schedule UUID: " + scheduleUUID);
    }
    return schedule;
  }

  public static List<Schedule> getAll() {
    return find.query().findList();
  }

  public static List<Schedule> getAllActiveByCustomerUUID(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).eq("status", "Active").findList();
  }

  public static List<Schedule> getAllActive() {
    return find.query().where().eq("status", "Active").findList();
  }

  public static List<Schedule> getActiveBackupSchedules(UUID customerUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("status", "Active")
        .in("task_type", TaskType.BackupUniverse, TaskType.MultiTableBackup)
        .findList();
  }

  public static List<Schedule> findAllScheduleWithCustomerConfig(UUID customerConfigUUID) {
    List<Schedule> scheduleList =
        find.query()
            .where()
            .or()
            .eq("task_type", TaskType.BackupUniverse)
            .eq("task_type", TaskType.MultiTableBackup)
            .endOr()
            .or()
            .eq("status", "Paused")
            .eq("status", "Active")
            .endOr()
            .findList();
    // This should be safe to do since storageConfigUUID is a required constraint.
    scheduleList =
        scheduleList
            .stream()
            .filter(
                s ->
                    s.getTaskParams()
                        .path("storageConfigUUID")
                        .asText()
                        .equals(customerConfigUUID.toString()))
            .collect(Collectors.toList());
    return scheduleList;
  }

  public static SchedulePagedApiResponse pagedList(SchedulePagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.taskType);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Query<Schedule> query = createQueryByFilter(pagedQuery.getFilter()).query();
    SchedulePagedResponse response =
        performPagedQuery(query, pagedQuery, SchedulePagedResponse.class);
    return createResponse(response);
  }

  public static ExpressionList<Schedule> createQueryByFilter(ScheduleFilter filter) {
    ExpressionList<Schedule> query =
        find.query().setPersistenceContextScope(PersistenceContextScope.QUERY).where();
    query.eq("customer_uuid", filter.getCustomerUUID());
    appendInClause(query, "status", filter.getStatus());
    appendInClause(query, "task_type", filter.getTaskTypes());
    if (!CollectionUtils.isEmpty(filter.getUniverseUUIDList())) {
      appendInClause(query, "owner_uuid", filter.getUniverseUUIDList());
    }
    return query;
  }

  public static Schedule getScheduleByUniverseWithName(
      String scheduleName, UUID universeUUID, UUID customerUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("owner_uuid", universeUUID)
        .eq("schedule_name", scheduleName)
        .findOne();
  }

  private static SchedulePagedApiResponse createResponse(SchedulePagedResponse response) {
    List<Schedule> schedules = response.getEntities();
    List<ScheduleResp> schedulesList =
        schedules.parallelStream().map(s -> toScheduleResp(s)).collect(Collectors.toList());
    SchedulePagedApiResponse responseMin;
    try {
      responseMin = SchedulePagedApiResponse.class.newInstance();
    } catch (Exception e) {
      throw new IllegalStateException(
          "Failed to create " + SchedulePagedApiResponse.class.getSimpleName() + " instance", e);
    }
    responseMin.setEntities(schedulesList);
    responseMin.setHasPrev(response.isHasPrev());
    responseMin.setHasNext(response.isHasNext());
    responseMin.setTotalCount(response.getTotalCount());
    return responseMin;
  }

  private static ScheduleResp toScheduleResp(Schedule schedule) {
    ScheduleRespBuilder builder =
        ScheduleResp.builder()
            .scheduleName(schedule.scheduleName)
            .scheduleUUID(schedule.scheduleUUID)
            .customerUUID(schedule.customerUUID)
            .failureCount(schedule.failureCount)
            .frequency(schedule.frequency)
            .frequencyTimeUnit(schedule.frequencyTimeUnit)
            .taskType(schedule.taskType)
            .status(schedule.status)
            .cronExpression(schedule.cronExpression)
            .runningState(schedule.runningState)
            .failureCount(schedule.failureCount);

    ScheduleTask lastTask = ScheduleTask.getLastTask(schedule.getScheduleUUID());
    Date lastScheduledTime = null;
    if (lastTask != null) {
      lastScheduledTime = lastTask.getScheduledTime();
      builder.prevCompletedTask(lastScheduledTime);
      builder.nextExpectedTask(nextExpectedTaskTime(lastScheduledTime, schedule));
    } else {
      builder.nextExpectedTask(nextExpectedTaskTime(lastScheduledTime, schedule));
    }

    JsonNode scheduleTaskParams = schedule.taskParams;
    if (!schedule.taskType.equals(TaskType.ExternalScript)) {
      ObjectMapper mapper = new ObjectMapper();
      if (Util.canConvertJsonNode(scheduleTaskParams, BackupRequestParams.class)) {
        BackupRequestParams params =
            mapper.convertValue(scheduleTaskParams, BackupRequestParams.class);
        builder.backupInfo(getV2ScheduleBackupInfo(params));
      } else if (Util.canConvertJsonNode(scheduleTaskParams, MultiTableBackup.Params.class)) {
        MultiTableBackup.Params params =
            mapper.convertValue(scheduleTaskParams, MultiTableBackup.Params.class);
        builder.backupInfo(getV1ScheduleBackupInfo(params));
      } else {
        LOG.error(
            "Could not parse backup taskParams {} for schedule {}",
            scheduleTaskParams,
            schedule.scheduleUUID);
      }
    } else {
      builder.taskParams(scheduleTaskParams);
    }
    return builder.build();
  }

  private static Date nextExpectedTaskTime(Date lastScheduledTime, Schedule schedule) {
    long nextScheduleTime;
    if (schedule.cronExpression == null) {
      if (lastScheduledTime != null) {
        nextScheduleTime = lastScheduledTime.getTime() + schedule.frequency;
      } else {
        // The task will be definitely executed under 2 minutes (scheduler frequency).
        return DateUtils.addMinutes(new Date(), 2);
      }
    } else {
      lastScheduledTime = new Date();
      CronParser unixCronParser = new CronParser(CronDefinitionBuilder.instanceDefinitionFor(UNIX));
      ExecutionTime executionTime =
          ExecutionTime.forCron(unixCronParser.parse(schedule.cronExpression));
      ZoneId defaultZoneId = ZoneId.systemDefault();
      Instant instant = lastScheduledTime.toInstant();
      ZonedDateTime zonedDateTime = instant.atZone(defaultZoneId);
      Duration duration = executionTime.timeToNextExecution(zonedDateTime).get();
      nextScheduleTime = lastScheduledTime.getTime() + duration.toMillis();
    }
    return new Date(nextScheduleTime);
  }

  private static BackupInfo getV2ScheduleBackupInfo(BackupRequestParams params) {
    List<KeyspaceTablesList> keySpaceResponseList = null;
    if (params.keyspaceTableList != null) {
      keySpaceResponseList = new ArrayList<>();
      for (KeyspaceTable keyspaceTable : params.keyspaceTableList) {
        KeyspaceTablesListBuilder keySpaceTableListBuilder =
            KeyspaceTablesList.builder().keyspace(keyspaceTable.keyspace);
        if (keyspaceTable.tableUUIDList != null) {
          keySpaceTableListBuilder.tableUUIDList(keyspaceTable.tableUUIDList);
        }
        if (keyspaceTable.tableNameList != null) {
          keySpaceTableListBuilder.tablesList(keyspaceTable.tableNameList);
        }
        keySpaceResponseList.add(keySpaceTableListBuilder.build());
      }
    }
    BackupInfo backupInfo =
        BackupInfo.builder()
            .universeUUID(params.universeUUID)
            .keyspaceList(keySpaceResponseList)
            .storageConfigUUID(params.storageConfigUUID)
            .backupType(params.backupType)
            .isTableByTableBackup(params.tableByTableBackup)
            .timeBeforeDelete(params.timeBeforeDelete)
            .fullBackup(CollectionUtils.isEmpty(params.keyspaceTableList))
            .useTablespaces(params.useTablespaces)
            .expiryTimeUnit(params.expiryTimeUnit)
            .parallelism(params.parallelism)
            .build();
    return backupInfo;
  }

  private static BackupInfo getV1ScheduleBackupInfo(MultiTableBackup.Params params) {
    List<KeyspaceTablesList> keySpaceResponseList = null;
    if (!StringUtils.isEmpty(params.getKeyspace())) {
      KeyspaceTablesList kTList =
          KeyspaceTablesList.builder()
              .keyspace(params.getKeyspace())
              .tablesList(params.getTableNameList())
              .tableUUIDList(params.getTableUUIDList())
              .build();
      keySpaceResponseList = Arrays.asList(kTList);
    }
    BackupInfo backupInfo =
        BackupInfo.builder()
            .universeUUID(params.universeUUID)
            .keyspaceList(keySpaceResponseList)
            .storageConfigUUID(params.storageConfigUUID)
            .backupType(params.backupType)
            .timeBeforeDelete(params.timeBeforeDelete)
            .expiryTimeUnit(params.expiryTimeUnit)
            .fullBackup(StringUtils.isEmpty(params.getKeyspace()))
            .useTablespaces(params.useTablespaces)
            .parallelism(params.parallelism)
            .build();
    return backupInfo;
  }
}
