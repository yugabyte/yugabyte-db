// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.cronutils.model.CronType.UNIX;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.ScheduleResp.BackupInfo;
import com.yugabyte.yw.models.ScheduleResp.ScheduleRespBuilder;
import com.yugabyte.yw.models.filters.ScheduleFilter;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList.KeyspaceTablesListBuilder;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.helpers.TransactionUtil;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import com.yugabyte.yw.models.paging.SchedulePagedApiResponse;
import com.yugabyte.yw.models.paging.SchedulePagedQuery;
import com.yugabyte.yw.models.paging.SchedulePagedResponse;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.Query;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import jakarta.persistence.UniqueConstraint;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.Setter;
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
@Getter
@Setter
public class Schedule extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Schedule.class);

  // This is a key lock for Schedule by UUID.
  public static final KeyLock<UUID> SCHEDULE_KEY_LOCK = new KeyLock<UUID>();

  public enum State {
    @EnumValue("Deleting")
    Deleting("Error"),

    @EnumValue("Error")
    Error("Deleting", "Creating", "Editing"),

    @EnumValue("Active")
    Active("Editing", "Deleting", "Stopped"),

    @EnumValue("Creating")
    Creating("Active", "Error", "Deleting"),

    @EnumValue("Paused")
    Paused(),

    @EnumValue("Stopped")
    Stopped("Active", "Deleting"),

    @EnumValue("Editing")
    Editing("Active", "Deleting", "Error");

    private final String[] allowedTransitions;

    State(String... allowedTransitions) {
      this.allowedTransitions = allowedTransitions;
    }

    public Set<State> allowedTransitionStates() {
      return Arrays.asList(allowedTransitions).stream()
          .map(s -> State.valueOf(s))
          .collect(Collectors.toSet());
    }

    public boolean stateTransitionAllowed(State state) {
      return this.equals(state) || allowedTransitionStates().contains(state);
    }

    public boolean isIntermediateState() {
      return this.equals(Deleting) || this.equals(Creating) || this.equals(Editing);
    }
  }

  public enum SortBy implements PagedQuery.SortByIF {
    taskType("taskType"),
    scheduleUUID("scheduleUUID"),
    scheduleName("scheduleName");

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
  private UUID scheduleUUID;

  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID customerUUID;

  @JsonProperty
  @JsonIgnore
  public void setCustomerUUID(UUID customerUUID) {
    this.customerUUID = customerUUID;
    ObjectNode scheduleTaskParams = (ObjectNode) getTaskParams();
    scheduleTaskParams.set("customerUUID", Json.toJson(customerUUID));
  }

  @ApiModelProperty(value = "Number of failed backup attempts", accessMode = READ_ONLY)
  @Column(nullable = false, columnDefinition = "integer default 0")
  private int failureCount;

  @ApiModelProperty(value = "Frequency of the schedule, in milli seconds", accessMode = READ_WRITE)
  @Column(nullable = false)
  private long frequency;

  @ApiModelProperty(value = "Request body for the task", accessMode = READ_ONLY)
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  private JsonNode taskParams;

  @ApiModelProperty(value = "Type of task to be scheduled.", accessMode = READ_WRITE)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private TaskType taskType;

  @ApiModelProperty(
      value = "Status of the task. Possible values are _Active_, _Paused_, or _Stopped_.",
      accessMode = READ_ONLY)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private State status = State.Active;

  @Column
  @ApiModelProperty(value = "Cron expression for the schedule")
  private String cronExpression;

  @Column(nullable = false)
  @ApiModelProperty(value = "Whether to use local timezone with cron expression for the schedule")
  private boolean useLocalTimezone = true;

  @ApiModelProperty(value = "Name of the schedule", accessMode = READ_ONLY)
  @Column(nullable = false)
  private String scheduleName;

  @ApiModelProperty(value = "Owner UUID for the schedule", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID ownerUUID;

  @ApiModelProperty(value = "Time unit of frequency", accessMode = READ_WRITE)
  private TimeUnit frequencyTimeUnit;

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Time on which schedule is expected to run",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  private Date nextScheduleTaskTime;

  public void updateNextScheduleTaskTime(Date nextScheduleTime) {
    this.nextScheduleTaskTime = nextScheduleTime;
    save();
  }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Time on which schedule is expected to run for incremental backups",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  private Date nextIncrementScheduleTaskTime;

  public void updateNextIncrementScheduleTaskTime(Date nextIncrementScheduleTime) {
    this.nextIncrementScheduleTaskTime = nextIncrementScheduleTime;
    save();
  }

  @ApiModelProperty(
      value = "Backlog status of schedule arose due to conflicts",
      accessMode = READ_ONLY)
  @Column(nullable = false)
  private boolean backlogStatus;

  @ApiModelProperty(
      value = "Backlog status of schedule of incremental backups arose due to conflicts",
      accessMode = READ_ONLY)
  @Column(nullable = false)
  private boolean incrementBacklogStatus;

  @Column
  @ApiModelProperty(value = "User who created the schedule policy", accessMode = READ_ONLY)
  private String userEmail;

  /* ---- Locking Schedule updater ---- */
  public interface ScheduleUpdater {
    void run(Schedule schedule);
  }

  public static Schedule saveDetails(
      UUID customerUUID, UUID scheduleUUID, ScheduleUpdater updater) {
    SCHEDULE_KEY_LOCK.acquireLock(scheduleUUID);
    try {
      AtomicReference<Schedule> scheduleRef = new AtomicReference<>();
      TransactionUtil.doInTxn(
          () -> {
            Schedule schedule = getOrBadRequest(customerUUID, scheduleUUID);
            updater.run(schedule);
            schedule.save();
            scheduleRef.set(schedule);
          },
          TransactionUtil.DEFAULT_RETRY_CONFIG);
      return scheduleRef.get();
    } finally {
      SCHEDULE_KEY_LOCK.releaseLock(scheduleUUID);
    }
  }

  /* ---- Locking Schedule updater end ---- */

  public static Schedule modifyScheduleRunningAndSave(
      UUID customerUUID, UUID scheduleUUID, boolean isRunning) {
    return modifyScheduleRunningAndSave(
        customerUUID, scheduleUUID, isRunning, false /* onlyLockIfActive */);
  }

  public static Schedule modifyScheduleRunningAndSave(
      UUID customerUUID, UUID scheduleUUID, boolean isRunning, boolean onlyLockIfActive) {
    ScheduleUpdater updater =
        s -> {
          if (onlyLockIfActive && (s.status != State.Active)) {
            LOG.error("Schedule is not active");
            throw new RuntimeException("Schedule is not active");
          } else if (isRunning && s.isRunningState()) {
            LOG.error("Schedule is currently locked");
            throw new RuntimeException("Schedule is currently locked");
          } else {
            s.setRunningState(isRunning);
          }
        };
    return saveDetails(customerUUID, scheduleUUID, updater);
  }

  public void updateBacklogStatus(boolean backlogStatus) {
    this.backlogStatus = backlogStatus;
    save();
  }

  public void updateIncrementBacklogStatus(boolean incrementBacklogStatus) {
    this.incrementBacklogStatus = incrementBacklogStatus;
    save();
  }

  public void setCronExpressionAndTaskParams(String cronExpression, ITaskParams params) {
    this.cronExpression = cronExpression;
    this.taskParams = Json.toJson(params);
  }

  public void setFailureCount(int count) {
    this.failureCount = count;
    if (count >= MAX_FAIL_COUNT) {
      this.status = State.Paused;
    }
  }

  public void resetSchedule() {
    this.status = State.Active;
    // Update old next Expected Task time if it expired due to non-active state.
    if (this.nextScheduleTaskTime == null || Util.isTimeExpired(this.nextScheduleTaskTime)) {
      updateNextScheduleTaskTime(nextExpectedTaskTime(null /* lastScheduledTime */));
    }
    save();
  }

  public void resetIncrementSchedule() {
    this.status = State.Active;
    // Update old next Expected Task time if it expired due to non-active state.
    if (this.nextIncrementScheduleTaskTime == null
        || Util.isTimeExpired(this.nextIncrementScheduleTaskTime)) {
      updateNextIncrementScheduleTaskTime(ScheduleUtil.nextExpectedIncrementTaskTime(this));
    }
    save();
  }

  // Use locking updater based status change.
  @Deprecated
  public void stopSchedule() {
    this.status = State.Stopped;
    save();
  }

  private void updateStatus(State state) {
    if (!this.status.stateTransitionAllowed(state)) {
      throw new RuntimeException(
          String.format("Transition of Schedule from %s to %s not allowed.", this.status, state));
    }
    this.status = state;
  }

  public static Schedule updateStatusAndSave(UUID customerUUID, UUID scheduleUUID, State state) {
    ScheduleUpdater updater = s -> s.updateStatus(state);
    return saveDetails(customerUUID, scheduleUUID, updater);
  }

  private void updateNewBackupScheduleTimes() {
    ScheduleTask lastTask = ScheduleTask.getLastTask(this.getScheduleUUID());
    Date nextScheduleTaskTime = null;
    if (lastTask == null || Util.isTimeExpired(nextExpectedTaskTime(lastTask.getScheduledTime()))) {
      nextScheduleTaskTime = nextExpectedTaskTime(null /* lastScheduledTime */);
    } else {
      nextScheduleTaskTime = nextExpectedTaskTime(lastTask.getScheduledTime());
    }
    Date nextExpectedIncrementScheduleTaskTime = null;
    long incrementalBackupFrequency =
        Json.fromJson(this.taskParams, BackupRequestParams.class).incrementalBackupFrequency;
    if (incrementalBackupFrequency != 0L) {
      nextExpectedIncrementScheduleTaskTime = ScheduleUtil.nextExpectedIncrementTaskTime(this);
    }
    this.nextScheduleTaskTime = nextScheduleTaskTime;
    this.nextIncrementScheduleTaskTime = nextExpectedIncrementScheduleTaskTime;
  }

  // Toggle Schedule state
  public static Schedule toggleBackupSchedule(
      UUID customerUUID, UUID scheduleUUID, State newState) {
    ScheduleUpdater updater =
        s -> {
          if (newState == s.status) {
            throw new RuntimeException(String.format("Schedule is already %s", s.status));
          }
          if (s.isRunningState() && newState == State.Stopped) {
            throw new RuntimeException("Schedule is currently locked");
          }
          s.updateStatus(newState);
          if (newState == State.Active) {
            s.updateNewBackupScheduleTimes();
          }
        };
    return saveDetails(customerUUID, scheduleUUID, updater);
  }

  // On frequency or cron expression change, modify new full/incremental backup schedule time and
  // save
  public static Schedule updateNewBackupScheduleTimeAndStatusAndSave(
      UUID customerUUID, UUID scheduleUUID, State newState, BackupRequestParams newScheduleParams) {
    ScheduleUpdater updater =
        s -> {
          if (newScheduleParams.schedulingFrequency > 0L) {
            s.frequency = newScheduleParams.schedulingFrequency;
            s.frequencyTimeUnit = newScheduleParams.frequencyTimeUnit;
            s.cronExpression = null;
          } else if (StringUtils.isNotBlank(newScheduleParams.cronExpression)) {
            s.cronExpression = newScheduleParams.cronExpression;
            s.frequency = 0L;
          }
          s.setTaskParams(Json.toJson(newScheduleParams));
          s.updateNewBackupScheduleTimes();
          s.updateStatus(newState);
        };
    return saveDetails(customerUUID, scheduleUUID, updater);
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
    setFrequencyTimeUnit(frequencyTimeUnit);
    save();
  }

  @Column(nullable = false)
  @ApiModelProperty(value = "Running state of the schedule")
  private boolean runningState = false;

  public void updateIncrementalBackupFrequencyAndTimeUnit(
      long incrementalBackupFrequency, TimeUnit incrementalBackupFrequencyTimeUnit) {
    ObjectMapper mapper = new ObjectMapper();
    BackupRequestParams params = mapper.convertValue(getTaskParams(), BackupRequestParams.class);
    params.incrementalBackupFrequency = incrementalBackupFrequency;
    params.incrementalBackupFrequencyTimeUnit = incrementalBackupFrequencyTimeUnit;
    this.taskParams = Json.toJson(params);
    save();
    resetIncrementSchedule();
  }

  public static final Finder<UUID, Schedule> find = new Finder<UUID, Schedule>(Schedule.class) {};

  public static Schedule getOrCreateSchedule(
      UUID customerUUID, BackupRequestParams scheduleParams) {
    Schedule schedule = null;
    Optional<Schedule> optionalSchedule =
        Schedule.maybeGetScheduleByUniverseWithName(
            scheduleParams.scheduleName,
            scheduleParams.getUniverseUUID(),
            scheduleParams.customerUUID);
    if (!optionalSchedule.isPresent()) {
      schedule =
          Schedule.create(
              scheduleParams.customerUUID,
              scheduleParams.getUniverseUUID(),
              scheduleParams,
              TaskType.CreateBackup,
              scheduleParams.schedulingFrequency,
              scheduleParams.cronExpression,
              scheduleParams.useLocalTimezone,
              scheduleParams.frequencyTimeUnit,
              scheduleParams.scheduleName,
              State.Creating);
    } else {
      schedule = optionalSchedule.get();
    }
    return schedule;
  }

  public static Schedule create(
      UUID customerUUID,
      UUID ownerUUID,
      ITaskParams params,
      TaskType taskType,
      long frequency,
      String cronExpression,
      boolean useLocalTimezone,
      TimeUnit frequencyTimeUnit,
      String scheduleName) {
    return create(
        customerUUID,
        ownerUUID,
        params,
        taskType,
        frequency,
        cronExpression,
        useLocalTimezone,
        frequencyTimeUnit,
        scheduleName,
        State.Active);
  }

  public static Schedule create(
      UUID customerUUID,
      UUID ownerUUID,
      ITaskParams params,
      TaskType taskType,
      long frequency,
      String cronExpression,
      boolean useLocalTimezone,
      TimeUnit frequencyTimeUnit,
      String scheduleName,
      State status) {
    Schedule schedule = new Schedule();
    schedule.setScheduleUUID(UUID.randomUUID());
    schedule.customerUUID = customerUUID;
    schedule.failureCount = 0;
    schedule.taskType = taskType;
    schedule.taskParams = Json.toJson(params);
    schedule.frequency = frequency;
    schedule.status = status;
    schedule.cronExpression = cronExpression;
    schedule.setUseLocalTimezone(useLocalTimezone);
    schedule.ownerUUID = ownerUUID;
    schedule.frequencyTimeUnit = frequencyTimeUnit;
    schedule.userEmail = Util.maybeGetEmailFromContext();
    schedule.scheduleName =
        scheduleName != null ? scheduleName : "schedule-" + schedule.getScheduleUUID();
    schedule.nextScheduleTaskTime = schedule.nextExpectedTaskTime(null /* lastScheduledTime */);
    schedule.nextIncrementScheduleTaskTime = ScheduleUtil.nextExpectedIncrementTaskTime(schedule);
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
        true /* useLocalTimezone */,
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

  private static Schedule get(UUID customerUUID, UUID scheduleUUID) {
    return find.query().where().idEq(scheduleUUID).eq("customer_uuid", customerUUID).findOne();
  }

  public static Optional<Schedule> maybeGet(UUID scheduleUUID) {
    return Optional.ofNullable(get(scheduleUUID));
  }

  public static Optional<Schedule> maybeGet(UUID customerUUID, UUID scheduleUUID) {
    return Optional.ofNullable(get(customerUUID, scheduleUUID));
  }

  public static Schedule getOrBadRequest(UUID scheduleUUID) {
    Schedule schedule = get(scheduleUUID);
    if (schedule == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Schedule UUID: " + scheduleUUID);
    }
    return schedule;
  }

  public static Schedule getOrBadRequest(UUID customerUUID, UUID scheduleUUID) {
    Schedule schedule = get(customerUUID, scheduleUUID);
    if (schedule == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid Customer UUID: " + customerUUID + ", Invalid Schedule UUID: " + scheduleUUID);
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

  public static List<Schedule> getAllActiveSchedulesByOwnerUUIDAndType(
      UUID ownerUUID, TaskType taskType) {
    return find.query()
        .where()
        .eq("owner_uuid", ownerUUID)
        .eq("status", "Active")
        .eq("task_type", taskType)
        .findList();
  }

  public static List<Schedule> getAllSchedulesByOwnerUUID(UUID ownerUUID) {
    return find.query().where().eq("owner_uuid", ownerUUID).findList();
  }

  public static List<Schedule> getAllSchedulesByOwnerUUIDAndType(
      UUID ownerUUID, TaskType taskType) {
    return find.query().where().eq("owner_uuid", ownerUUID).in("task_type", taskType).findList();
  }

  public static List<Schedule> getAllByCustomerUUIDAndType(UUID customerUUID, TaskType taskType) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .in("task_type", taskType)
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
        scheduleList.stream()
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

  public static Optional<Schedule> maybeGetScheduleByUniverseWithName(
      String scheduleName, UUID universeUUID, UUID customerUUID) {
    Schedule schedule = getScheduleByUniverseWithName(scheduleName, universeUUID, customerUUID);
    if (schedule == null) {
      return Optional.empty();
    }
    return Optional.of(schedule);
  }

  private static SchedulePagedApiResponse createResponse(SchedulePagedResponse response) {
    List<Schedule> schedules = response.getEntities();
    List<ScheduleResp> schedulesList =
        schedules.stream().map(Schedule::toScheduleResp).collect(Collectors.toList());
    SchedulePagedApiResponse responseMin = new SchedulePagedApiResponse();
    responseMin.setEntities(schedulesList);
    responseMin.setHasPrev(response.isHasPrev());
    responseMin.setHasNext(response.isHasNext());
    responseMin.setTotalCount(response.getTotalCount());
    return responseMin;
  }

  private static ScheduleResp toScheduleResp(Schedule schedule) {
    boolean isIncrementalBackup =
        ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID());
    Date nextScheduleTaskTime = schedule.nextScheduleTaskTime;
    Date nextIncrementScheduleTaskTime = schedule.nextIncrementScheduleTaskTime;
    // In case of a schedule with a backlog, the next task can be executed in the next scheduler
    // run.
    if (schedule.backlogStatus) {
      nextScheduleTaskTime = DateUtils.addMinutes(new Date(), Util.YB_SCHEDULER_INTERVAL);
    }
    if (isIncrementalBackup && schedule.incrementBacklogStatus) {
      nextIncrementScheduleTaskTime = DateUtils.addMinutes(new Date(), Util.YB_SCHEDULER_INTERVAL);
    }
    // No need to show the next expected task time as it won't be able to execute due to non-active
    // state.
    if (!schedule.getStatus().equals(State.Active)) {
      nextScheduleTaskTime = null;
      nextIncrementScheduleTaskTime = null;
    }
    ScheduleRespBuilder builder =
        ScheduleResp.builder()
            .scheduleName(schedule.scheduleName)
            .scheduleUUID(schedule.getScheduleUUID())
            .customerUUID(schedule.customerUUID)
            .failureCount(schedule.failureCount)
            .frequency(schedule.frequency)
            .frequencyTimeUnit(schedule.frequencyTimeUnit)
            .taskType(schedule.taskType)
            .status(schedule.status)
            .cronExpression(schedule.cronExpression)
            .runningState(schedule.runningState)
            .failureCount(schedule.failureCount)
            .backlogStatus(schedule.backlogStatus)
            .incrementBacklogStatus(schedule.incrementBacklogStatus);

    ScheduleTask lastTask = ScheduleTask.getLastTask(schedule.getScheduleUUID());
    Date lastScheduledTime = null;
    if (lastTask != null) {
      lastScheduledTime = lastTask.getScheduledTime();
      builder.prevCompletedTask(lastScheduledTime);
    }

    JsonNode scheduleTaskParams = schedule.taskParams;
    if (!schedule.taskType.equals(TaskType.ExternalScript)) {
      if (Util.canConvertJsonNode(scheduleTaskParams, BackupRequestParams.class)) {
        BackupRequestParams params =
            Json.mapper().convertValue(scheduleTaskParams, BackupRequestParams.class);
        builder.backupInfo(getV2ScheduleBackupInfo(params));
        builder.incrementalBackupFrequency(params.incrementalBackupFrequency);
        builder.incrementalBackupFrequencyTimeUnit(params.incrementalBackupFrequencyTimeUnit);
        builder.tableByTableBackup(params.tableByTableBackup);
        if (isIncrementalBackup) {
          Backup latestSuccessfulIncrementalBackup =
              ScheduleUtil.fetchLatestSuccessfulBackupForSchedule(
                  schedule.customerUUID, schedule.getScheduleUUID());
          if (latestSuccessfulIncrementalBackup != null) {
            Date incrementalBackupExpectedTaskTime;
            if (nextIncrementScheduleTaskTime != null) {
              incrementalBackupExpectedTaskTime = nextIncrementScheduleTaskTime;
            } else {
              if (schedule.incrementBacklogStatus) {
                incrementalBackupExpectedTaskTime =
                    DateUtils.addMinutes(new Date(), Util.YB_SCHEDULER_INTERVAL);
              } else {
                incrementalBackupExpectedTaskTime =
                    new Date(
                        latestSuccessfulIncrementalBackup.getCreateTime().getTime()
                            + params.incrementalBackupFrequency);
              }
            }
            if (nextScheduleTaskTime != null
                && nextScheduleTaskTime.after(incrementalBackupExpectedTaskTime)) {
              nextScheduleTaskTime = incrementalBackupExpectedTaskTime;
            }
          }
        }
      } else if (Util.canConvertJsonNode(scheduleTaskParams, MultiTableBackup.Params.class)) {
        MultiTableBackup.Params params =
            Json.mapper().convertValue(scheduleTaskParams, MultiTableBackup.Params.class);
        builder.backupInfo(getV1ScheduleBackupInfo(params));
      } else {
        LOG.error(
            "Could not parse backup taskParams {} for schedule {}",
            scheduleTaskParams,
            schedule.getScheduleUUID());
      }
    } else {
      builder.taskParams(scheduleTaskParams);
    }
    builder.nextExpectedTask(nextScheduleTaskTime);
    return builder.build();
  }

  public Date nextExpectedTaskTime(@Nullable Date lastScheduledTime) {
    long nextScheduleTime;
    if (this.cronExpression == null) {
      if (lastScheduledTime != null) {
        nextScheduleTime = lastScheduledTime.getTime() + this.frequency;
      } else {
        // The task will be definitely executed under 2 minutes (scheduler frequency).
        return new Date();
      }
    } else {
      lastScheduledTime = new Date();
      CronParser unixCronParser = new CronParser(CronDefinitionBuilder.instanceDefinitionFor(UNIX));
      ExecutionTime executionTime =
          ExecutionTime.forCron(unixCronParser.parse(this.cronExpression));
      ZoneId defaultZoneId = this.isUseLocalTimezone() ? ZoneId.systemDefault() : ZoneId.of("UTC");
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
            .universeUUID(params.getUniverseUUID())
            .keyspaceList(keySpaceResponseList)
            .storageConfigUUID(params.storageConfigUUID)
            .backupType(params.backupType)
            .timeBeforeDelete(params.timeBeforeDelete)
            .fullBackup(CollectionUtils.isEmpty(params.keyspaceTableList))
            .useTablespaces(params.useTablespaces)
            .expiryTimeUnit(params.expiryTimeUnit)
            .parallelism(params.parallelism)
            .pointInTimeRestoreEnabled(params.enablePointInTimeRestore)
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
            .universeUUID(params.getUniverseUUID())
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

  /**
   * Returns max of history retention interval required for PIT enabled schedules.
   *
   * @param universeUUID
   * @param includeIntermediate If true, use intermediate schedule state - Creating/Editing when
   *     calculating the max value
   * @return Retention value in seconds
   */
  public static Duration getMaxBackupIntervalInUniverseForPITRestore(
      UUID universeUUID, boolean includeIntermediate) {
    List<Schedule> schedules =
        getAllSchedulesByOwnerUUIDAndType(universeUUID, TaskType.CreateBackup);
    List<State> states = new ArrayList<>(Arrays.asList(State.Active, State.Stopped));
    if (includeIntermediate) {
      states.add(State.Editing);
      states.add(State.Creating);
    }
    return schedules.stream()
        .filter(s -> states.contains(s.status))
        .map(s -> Json.fromJson(s.getTaskParams(), BackupRequestParams.class))
        .map(ScheduleUtil::getBackupIntervalForPITRestore)
        .max(Comparator.naturalOrder())
        .orElse(Duration.ofSeconds(0));
  }
}
