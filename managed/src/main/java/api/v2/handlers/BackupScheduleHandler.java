// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.BackupScheduleMapper;
import api.v2.mappers.StorageConfigMapper;
import api.v2.models.BackupSchedule;
import api.v2.models.BackupScheduleApiFilter;
import api.v2.models.BackupScheduleCronFrequency;
import api.v2.models.BackupScheduleDuration;
import api.v2.models.BackupScheduleIntervalFrequency;
import api.v2.models.BackupScheduleKeyspace;
import api.v2.models.BackupSchedulePagedQuerySpec;
import api.v2.models.BackupSchedulePagedResp;
import api.v2.models.BackupScheduleSpec;
import api.v2.models.BackupScheduleState;
import api.v2.models.BackupScheduleTableType;
import api.v2.models.YBATask;
import api.v2.utils.NormalizedPaginationSpec;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ScheduleTaskHelper;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.DB;
import io.ebean.DuplicateKeyException;
import io.ebean.ExpressionList;
import io.ebean.PagedList;
import io.ebean.SqlRow;
import java.util.Collection;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

/**
 * v2 backup schedules: the v1 schedules whose task is a backup.
 *
 * <p>Create runs on the request only the checks that need no storage or universe RPC, saves the
 * schedule in state Creating and returns the v1 CreateBackupSchedule task, which checks the storage
 * and makes the schedule Active. When the create fails, the task removes the schedule so the name
 * is free for a retry. Status codes, unlike v1: 400 for an invalid spec, 404 for a missing universe
 * or storage config, and 409 with a code for a name conflict (also when the DB unique constraint
 * catches a concurrent create) or a storage config that isn't Active.
 *
 * <p>Edit merges the frequency, incremental interval and retention of the spec into the stored
 * params and returns the v1 EditBackupSchedule task; every other field must be unchanged. Unlike
 * v1's 400s, a schedule another task holds or a locked universe is a 409 with a code.
 */
@Singleton
public class BackupScheduleHandler {

  public static final String NAME_CONFLICT = "BACKUP_SCHEDULE_NAME_CONFLICT";
  public static final String STORAGE_CONFIG_NOT_ACTIVE = StorageConfigHandler.NOT_ACTIVE;
  public static final String BUSY = "BACKUP_SCHEDULE_BUSY";
  public static final String UNIVERSE_LOCKED = "UNIVERSE_LOCKED";

  /** The states v1 lets the edit task move to Editing from. */
  private static final Set<Schedule.State> EDITABLE_STATES =
      EnumSet.of(Schedule.State.Active, Schedule.State.Stopped, Schedule.State.Error);

  private static final String BACKUP_TYPE_PARAM = "task_params->>'backupType'";
  private static final List<String> V2_BACKUP_TYPES =
      List.of(
          TableType.PGSQL_TABLE_TYPE.name(),
          TableType.YQL_TABLE_TYPE.name(),
          TableType.REDIS_TABLE_TYPE.name());

  private final ScheduleTaskHelper scheduleTaskHelper;
  private final BackupHelper backupHelper;

  @Inject
  public BackupScheduleHandler(ScheduleTaskHelper scheduleTaskHelper, BackupHelper backupHelper) {
    this.scheduleTaskHelper = scheduleTaskHelper;
    this.backupHelper = backupHelper;
  }

  public YBATask createBackupSchedule(UUID cUUID, BackupScheduleSpec spec) {
    Customer customer = Customer.getOrNotFound(cUUID);
    checkSpec(spec);
    Universe universe = Universe.getOrNotFound(spec.getUniverseUuid(), cUUID);
    CustomerConfig storageConfig = CustomerConfig.get(cUUID, spec.getStorageConfigUuid());
    if (storageConfig == null || storageConfig.getType() != ConfigType.STORAGE) {
      throw new PlatformServiceException(
          NOT_FOUND, "No storage configuration with UUID " + spec.getStorageConfigUuid());
    }
    if (!storageConfig.isActive()) {
      ObjectNode error = Json.newObject();
      error.put("code", STORAGE_CONFIG_NOT_ACTIVE);
      error.put(
          "message",
          String.format(
              "Storage configuration %s is %s, not ACTIVE",
              storageConfig.getConfigUUID(),
              StorageConfigMapper.INSTANCE.toStorageConfigInfo(storageConfig).getState()));
      error.put("storage_config_uuid", storageConfig.getConfigUUID().toString());
      throw new PlatformServiceException(CONFLICT, error);
    }
    BackupRequestParams params = BackupScheduleMapper.toBackupRequestParams(cUUID, spec);
    // Frequencies, cron, incremental and point-in-time rules; no I/O.
    params.validateScheduleParams(backupHelper, universe);
    Schedule.maybeGetScheduleByUniverseWithName(
            params.scheduleName, universe.getUniverseUUID(), cUUID)
        .ifPresent(
            existing -> {
              throw nameConflict(existing);
            });

    Schedule schedule;
    try {
      schedule =
          Schedule.create(
              cUUID,
              universe.getUniverseUUID(),
              params,
              TaskType.CreateBackup,
              params.schedulingFrequency,
              params.cronExpression,
              params.useLocalTimezone,
              params.frequencyTimeUnit,
              params.scheduleName,
              Schedule.State.Creating);
    } catch (DuplicateKeyException e) {
      // A concurrent request created a schedule with the same name after the check above.
      throw Schedule.maybeGetScheduleByUniverseWithName(
              params.scheduleName, universe.getUniverseUUID(), cUUID)
          .map(BackupScheduleHandler::nameConflict)
          .orElseThrow(() -> e);
    }

    BackupScheduleTaskParams taskParams =
        Json.mapper().convertValue(universe.getUniverseDetails(), BackupScheduleTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.creatingUser = CommonUtils.getUserFromContext();
    taskParams.setCustomerUUID(cUUID);
    taskParams.setScheduleParams(params);
    taskParams.createdScheduleUUID = schedule.getScheduleUUID();
    taskParams.validateStorageConfigInTask = true;
    UUID taskUUID;
    try {
      taskUUID = scheduleTaskHelper.createCreateScheduledBackupTask(taskParams, customer, universe);
    } catch (RuntimeException e) {
      // No task will set it up.
      BackupScheduleTaskParams.removeCreatingSchedule(cUUID, schedule.getScheduleUUID());
      throw e;
    }
    return new YBATask().taskUuid(taskUUID).resourceUuid(schedule.getScheduleUUID());
  }

  public YBATask editBackupSchedule(UUID cUUID, UUID bsUUID, BackupScheduleSpec spec) {
    Customer customer = Customer.getOrNotFound(cUUID);
    Schedule schedule = getSchedule(cUUID, bsUUID);
    if (schedule.getTaskType() != TaskType.CreateBackup) {
      throw badRequest(
          "Schedules of the older backup tasks (BackupUniverse, MultiTableBackup) can't be"
              + " edited");
    }
    checkSpec(spec);
    BackupScheduleSpec current = BackupScheduleMapper.INSTANCE.toBackupScheduleSpec(schedule);
    checkUnchanged(current, spec, schedule);
    BackupRequestParams requested = BackupScheduleMapper.toBackupRequestParams(cUUID, spec);
    if (current.getRetention() != null && requested.timeBeforeDelete <= 0L) {
      throw badRequest("The retention can't be removed");
    }
    if (!EDITABLE_STATES.contains(schedule.getStatus()) || schedule.isRunningState()) {
      throw busy(schedule);
    }
    Universe universe = Universe.getOrNotFound(schedule.getOwnerUUID(), cUUID);
    BackupRequestParams params = Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    if (params.enablePointInTimeRestore && universe.universeIsLocked()) {
      ObjectNode error = Json.newObject();
      error.put("code", UNIVERSE_LOCKED);
      error.put(
          "message",
          String.format(
              "Universe %s is locked by another task; a point-in-time-restore schedule can't be"
                  + " edited until it finishes",
              universe.getName()));
      error.put("universe_uuid", universe.getUniverseUUID().toString());
      throw new PlatformServiceException(CONFLICT, error);
    }
    // What v1's edit changes; the rest of the stored params stays.
    params.cronExpression = requested.cronExpression;
    params.schedulingFrequency = requested.schedulingFrequency;
    params.frequencyTimeUnit = requested.frequencyTimeUnit;
    params.incrementalBackupFrequency = requested.incrementalBackupFrequency;
    params.incrementalBackupFrequencyTimeUnit = requested.incrementalBackupFrequencyTimeUnit;
    if (requested.timeBeforeDelete > 0L) {
      params.timeBeforeDelete = requested.timeBeforeDelete;
      params.expiryTimeUnit = requested.expiryTimeUnit;
    }
    // Frequencies, cron, incremental and point-in-time rules; no I/O.
    params.validateScheduleEditParams(
        backupHelper, universe, current.getIncrementalInterval() != null);

    BackupScheduleTaskParams taskParams =
        Json.mapper().convertValue(universe.getUniverseDetails(), BackupScheduleTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.creatingUser = CommonUtils.getUserFromContext();
    taskParams.setCustomerUUID(cUUID);
    taskParams.setScheduleUUID(bsUUID);
    taskParams.setScheduleParams(params);
    UUID taskUUID =
        scheduleTaskHelper.createEditScheduledBackupTask(taskParams, customer, universe, schedule);
    return new YBATask().taskUuid(taskUUID).resourceUuid(bsUUID);
  }

  /**
   * The fields an edit can't change must be as the schedule has them. An omitted option counts as
   * false, and an omitted min_backups_to_retain as v1's default.
   */
  private static void checkUnchanged(
      BackupScheduleSpec current, BackupScheduleSpec spec, Schedule schedule) {
    checkUnchanged("name", current.getName(), spec.getName());
    checkUnchanged("universe_uuid", current.getUniverseUuid(), spec.getUniverseUuid());
    checkUnchanged(
        "storage_config_uuid", current.getStorageConfigUuid(), spec.getStorageConfigUuid());
    checkUnchanged("backup_type", current.getBackupType(), spec.getBackupType());
    checkUnchanged("keyspaces", keyspaces(current), keyspaces(spec));
    checkUnchanged("kms_config_uuid", current.getKmsConfigUuid(), spec.getKmsConfigUuid());
    checkUnchanged("sse", isTrue(current.getSse()), isTrue(spec.getSse()));
    checkUnchanged(
        "table_by_table_backup",
        isTrue(current.getTableByTableBackup()),
        isTrue(spec.getTableByTableBackup()));
    checkUnchanged(
        "point_in_time_restore",
        isTrue(current.getPointInTimeRestore()),
        isTrue(spec.getPointInTimeRestore()));
    checkUnchanged(
        "use_tablespaces", isTrue(current.getUseTablespaces()), isTrue(spec.getUseTablespaces()));
    checkUnchanged("use_roles", isTrue(current.getUseRoles()), isTrue(spec.getUseRoles()));
    checkUnchanged(
        "min_backups_to_retain",
        current.getMinBackupsToRetain(),
        spec.getMinBackupsToRetain() == null
            ? Integer.valueOf(Util.MIN_NUM_BACKUPS_TO_RETAIN)
            : spec.getMinBackupsToRetain());
    // The task doesn't change the schedule's timezone.
    if (spec.getFrequency() instanceof BackupScheduleCronFrequency cron) {
      checkUnchanged(
          "frequency.use_local_timezone",
          schedule.isUseLocalTimezone(),
          !Boolean.FALSE.equals(cron.getUseLocalTimezone()));
    }
  }

  private static void checkUnchanged(String field, Object current, Object requested) {
    if (!Objects.equals(current, requested)) {
      throw badRequest(field + " can't be changed");
    }
  }

  private static boolean isTrue(Boolean value) {
    return Boolean.TRUE.equals(value);
  }

  /** Keyspace to tables, in any order; an omitted table list is empty. */
  private static Map<String, Set<UUID>> keyspaces(BackupScheduleSpec spec) {
    Map<String, Set<UUID>> keyspaces = new HashMap<>();
    if (spec.getKeyspaces() != null) {
      for (BackupScheduleKeyspace keyspace : spec.getKeyspaces()) {
        keyspaces
            .computeIfAbsent(keyspace.getKeyspace(), k -> new HashSet<>())
            .addAll(keyspace.getTableUuids() == null ? List.of() : keyspace.getTableUuids());
      }
    }
    return keyspaces;
  }

  private static PlatformServiceException busy(Schedule schedule) {
    BackupScheduleState state = BackupScheduleMapper.toBackupScheduleState(schedule.getStatus());
    ObjectNode error = Json.newObject();
    error.put("code", BUSY);
    error.put(
        "message",
        schedule.isRunningState()
            ? String.format(
                "Backup schedule %s is held by another task; retry when it finishes",
                schedule.getScheduleName())
            : String.format("Backup schedule %s is %s", schedule.getScheduleName(), state));
    error.put("backup_schedule_state", state.toString());
    return new PlatformServiceException(CONFLICT, error);
  }

  /** The spec checks the v1 params can't express. */
  private static void checkSpec(BackupScheduleSpec spec) {
    if (spec.getName() == null
        || spec.getUniverseUuid() == null
        || spec.getStorageConfigUuid() == null
        || spec.getBackupType() == null
        || spec.getFrequency() == null) {
      throw badRequest(
          "name, universe_uuid, storage_config_uuid, backup_type and frequency are required");
    }
    if (spec.getBackupType() == BackupScheduleTableType.BackupScheduleTableTypeREDIS) {
      throw badRequest("backup_type must be YSQL or YCQL");
    }
    if (spec.getKeyspaces() != null) {
      for (BackupScheduleKeyspace keyspace : spec.getKeyspaces()) {
        if (StringUtils.isBlank(keyspace.getKeyspace())) {
          throw badRequest("Each keyspaces entry needs its keyspace");
        }
      }
    }
    if (spec.getFrequency() instanceof BackupScheduleIntervalFrequency interval) {
      checkDuration("frequency.interval", interval.getInterval());
    } else if (spec.getFrequency() instanceof BackupScheduleCronFrequency cron) {
      if (StringUtils.isBlank(cron.getCronExpression())) {
        throw badRequest("frequency.cron_expression is required");
      }
    }
    if (spec.getIncrementalInterval() != null) {
      checkDuration("incremental_interval", spec.getIncrementalInterval());
    }
    if (spec.getRetention() != null) {
      checkDuration("retention", spec.getRetention());
    }
  }

  private static void checkDuration(String field, BackupScheduleDuration duration) {
    if (duration == null || duration.getAmount() == null || duration.getUnit() == null) {
      throw badRequest(field + " needs an amount and a unit");
    }
    if (duration.getAmount() < 1) {
      throw badRequest(field + ".amount must be at least 1");
    }
    try {
      BackupScheduleMapper.toMillis(duration);
    } catch (IllegalArgumentException | ArithmeticException e) {
      throw badRequest(field + " must be a whole number of milliseconds that fits in 64 bits");
    }
  }

  private static PlatformServiceException nameConflict(Schedule existing) {
    ObjectNode error = Json.newObject();
    error.put("code", NAME_CONFLICT);
    error.put(
        "message",
        String.format("The universe already has a schedule named %s", existing.getScheduleName()));
    error.put("backup_schedule_uuid", existing.getScheduleUUID().toString());
    error.put(
        "backup_schedule_state",
        BackupScheduleMapper.toBackupScheduleState(existing.getStatus()).toString());
    return new PlatformServiceException(CONFLICT, error);
  }

  private static PlatformServiceException badRequest(String message) {
    return new PlatformServiceException(BAD_REQUEST, message);
  }

  public BackupSchedule getBackupSchedule(UUID cUUID, UUID bsUUID) {
    return toBackupSchedule(getSchedule(cUUID, bsUUID), lastTasks(List.of(bsUUID)));
  }

  /** The customer's backup schedule, or 404. */
  private static Schedule getSchedule(UUID cUUID, UUID bsUUID) {
    Schedule schedule = backupSchedules(cUUID).idEq(bsUUID).findOne();
    if (schedule == null) {
      throw new PlatformServiceException(NOT_FOUND, "No backup schedule with UUID " + bsUUID);
    }
    return schedule;
  }

  public BackupSchedulePagedResp pageListBackupSchedules(
      UUID cUUID, BackupSchedulePagedQuerySpec query) {
    NormalizedPaginationSpec paging = HandlerPagingSupport.normalize(query);
    ExpressionList<Schedule> where = backupSchedules(cUUID);
    BackupScheduleApiFilter filter = query.getFilter();
    if (filter != null) {
      if (filter.getName() != null) {
        where.eq("schedule_name", filter.getName().trim());
      }
      if (filter.getUniverseUuids() != null && !filter.getUniverseUuids().isEmpty()) {
        where.in("owner_uuid", filter.getUniverseUuids());
      }
      if (filter.getStorageConfigUuids() != null && !filter.getStorageConfigUuids().isEmpty()) {
        where.in(
            Schedule.STORAGE_CONFIG_UUID_PARAM, Schedule.toStrings(filter.getStorageConfigUuids()));
      }
      if (filter.getStates() != null && !filter.getStates().isEmpty()) {
        where.in("status", toV1States(filter.getStates()));
      }
    }
    PagedList<Schedule> page =
        HandlerPagingSupport.getPagedList(
            where,
            paging,
            String.format(
                "lower(schedule_name) %s, schedule_uuid %s", paging.order(), paging.order()));
    Map<UUID, LastTask> lastTasks =
        lastTasks(
            page.getList().stream().map(Schedule::getScheduleUUID).collect(Collectors.toList()));
    return HandlerPagingSupport.pagedResponse(
        new BackupSchedulePagedResp(), page, schedule -> toBackupSchedule(schedule, lastTasks));
  }

  /**
   * The backup schedules of a customer. Params without a known backup type, which back up nothing,
   * are left out: backup_type is required.
   */
  private static ExpressionList<Schedule> backupSchedules(UUID cUUID) {
    return Schedule.find
        .query()
        .where()
        .eq("customer_uuid", cUUID)
        .in("task_type", BackupUtil.BACKUP_TASK_TYPES)
        .in(BACKUP_TYPE_PARAM, V2_BACKUP_TYPES);
  }

  private static BackupSchedule toBackupSchedule(Schedule schedule, Map<UUID, LastTask> lastTasks) {
    LastTask lastTask = lastTasks.get(schedule.getScheduleUUID());
    return BackupScheduleMapper.INSTANCE.toBackupSchedule(
        schedule,
        lastTask == null ? null : lastTask.scheduledTime,
        lastTask != null && lastTask.completedTime == null);
  }

  private static List<Schedule.State> toV1States(Collection<BackupScheduleState> states) {
    return states.stream()
        .flatMap(state -> BackupScheduleMapper.toV1States(state).stream())
        .distinct()
        .collect(Collectors.toList());
  }

  /**
   * The last task of each schedule, in one query. A backup is running while that task has no
   * completed time, which is how the scheduler tells it too.
   */
  private static Map<UUID, LastTask> lastTasks(List<UUID> scheduleUUIDs) {
    Map<UUID, LastTask> tasks = new HashMap<>();
    if (scheduleUUIDs.isEmpty()) {
      return tasks;
    }
    List<SqlRow> rows =
        DB.sqlQuery(
                "select distinct on (schedule_uuid) schedule_uuid, scheduled_time, completed_time"
                    + " from schedule_task where schedule_uuid in (:uuids)"
                    + " order by schedule_uuid, scheduled_time desc")
            .setParameter("uuids", scheduleUUIDs)
            .findList();
    for (SqlRow row : rows) {
      tasks.put(
          row.getUUID("schedule_uuid"),
          new LastTask(row.getUtilDate("scheduled_time"), row.getUtilDate("completed_time")));
    }
    return tasks;
  }

  private static final class LastTask {
    final Date scheduledTime;
    final Date completedTime;

    LastTask(Date scheduledTime, Date completedTime) {
      this.scheduledTime = scheduledTime;
      this.completedTime = completedTime;
    }
  }
}
