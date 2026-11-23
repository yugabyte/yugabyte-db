// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.BackupSchedule;
import api.v2.models.BackupScheduleCronFrequency;
import api.v2.models.BackupScheduleDuration;
import api.v2.models.BackupScheduleFrequency;
import api.v2.models.BackupScheduleFrequencyType;
import api.v2.models.BackupScheduleInfo;
import api.v2.models.BackupScheduleIntervalFrequency;
import api.v2.models.BackupScheduleKeyspace;
import api.v2.models.BackupScheduleSpec;
import api.v2.models.BackupScheduleState;
import api.v2.models.BackupScheduleTableType;
import api.v2.models.TimeUnitType;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.annotation.Nullable;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.time.DateUtils;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;
import play.libs.Json;

/**
 * Converts a v1 backup {@link Schedule} to the v2 backup schedule resource.
 *
 * <p>The frequency, cron expression and timezone come from the schedule's columns, which edits
 * update. Everything else is only in the schedule's untyped JSON task params: {@code
 * BackupRequestParams} for CreateBackup schedules, and the older {@code BackupTableParams} of
 * BackupUniverse and MultiTableBackup schedules, which name a single keyspace instead of a list.
 * The params are read by property name so that both shapes map.
 *
 * <p>v1 keeps durations in milliseconds with the unit the client chose for display. A duration maps
 * to that unit when it is a whole number of it, else to the largest unit it is a whole number of.
 */
@Mapper(config = CentralConfig.class)
public interface BackupScheduleMapper {
  BackupScheduleMapper INSTANCE = Mappers.getMapper(BackupScheduleMapper.class);

  long SECOND = 1000L;
  long MINUTE = 60 * SECOND;
  long HOUR = 60 * MINUTE;
  long DAY = 24 * HOUR;

  /** Milliseconds per unit, a month being 30 days and a year 365, as YBA's v1 clients count. */
  Map<TimeUnit, Long> UNIT_MILLIS =
      Map.of(
          TimeUnit.MILLISECONDS, 1L,
          TimeUnit.SECONDS, SECOND,
          TimeUnit.MINUTES, MINUTE,
          TimeUnit.HOURS, HOUR,
          TimeUnit.DAYS, DAY,
          TimeUnit.MONTHS, 30 * DAY,
          TimeUnit.YEARS, 365 * DAY);

  /** Units tried, largest first, when a duration isn't a whole number of its v1 unit. */
  List<TimeUnit> FALLBACK_UNITS =
      List.of(
          TimeUnit.DAYS, TimeUnit.HOURS, TimeUnit.MINUTES, TimeUnit.SECONDS, TimeUnit.MILLISECONDS);

  @Mapping(target = "tableUuids", source = "tableUUIDList")
  BackupScheduleKeyspace toBackupScheduleKeyspace(KeyspaceTable keyspaceTable);

  /**
   * The v2 schedule. {@code lastBackupTime} is when the schedule last started a backup, looked up
   * by the caller (in one query for a page of schedules).
   */
  default BackupSchedule toBackupSchedule(Schedule schedule, @Nullable Date lastBackupTime) {
    return new BackupSchedule()
        .spec(toBackupScheduleSpec(schedule))
        .info(toBackupScheduleInfo(schedule, lastBackupTime));
  }

  default BackupScheduleSpec toBackupScheduleSpec(Schedule schedule) {
    JsonNode params = schedule.getTaskParams();
    BackupScheduleSpec spec =
        new BackupScheduleSpec()
            .name(schedule.getScheduleName())
            .universeUuid(schedule.getOwnerUUID())
            .storageConfigUuid(uuid(params, "storageConfigUUID"))
            .backupType(toBackupScheduleTableType(params.path("backupType").asText(null)))
            .keyspaces(toKeyspaces(params))
            .frequency(toFrequency(schedule))
            .incrementalInterval(
                toDuration(
                    params.path("incrementalBackupFrequency").asLong(0),
                    timeUnit(params, "incrementalBackupFrequencyTimeUnit")))
            .retention(
                toDuration(
                    params.path("timeBeforeDelete").asLong(0), timeUnit(params, "expiryTimeUnit")))
            .kmsConfigUuid(uuid(params, "kmsConfigUUID"))
            .sse(bool(params, "sse"))
            .tableByTableBackup(bool(params, "tableByTableBackup"))
            .pointInTimeRestore(bool(params, "enablePointInTimeRestore"))
            .useTablespaces(bool(params, "useTablespaces"))
            .useRoles(bool(params, "useRoles"));
    if (params.has("minNumBackupsToRetain")) {
      spec.minBackupsToRetain(params.get("minNumBackupsToRetain").asInt());
    }
    return spec;
  }

  default BackupScheduleInfo toBackupScheduleInfo(
      Schedule schedule, @Nullable Date lastBackupTime) {
    DateTimeMapper dates = new DateTimeMapper();
    BackupScheduleInfo info =
        new BackupScheduleInfo()
            .uuid(schedule.getScheduleUUID())
            .state(toBackupScheduleState(schedule.getStatus()))
            .backupRunning(schedule.isRunningState())
            .lastBackupTime(dates.toOffsetDateTime(lastBackupTime))
            .failureCount(schedule.getFailureCount())
            .isKubernetesOperatorControlled(schedule.isKubernetesOperatorControlled());
    // As the v1 schedule list shows them: due times only for an active schedule, and a backup
    // that is overdue (backlog) runs at the next scheduler run.
    if (schedule.getStatus() == Schedule.State.Active) {
      Date nextRun = DateUtils.addMinutes(new Date(), Util.YB_SCHEDULER_INTERVAL);
      info.nextBackupTime(
          dates.toOffsetDateTime(
              schedule.isBacklogStatus() ? nextRun : schedule.getNextScheduleTaskTime()));
      if (schedule.getTaskParams().path("incrementalBackupFrequency").asLong(0) > 0) {
        info.nextIncrementalBackupTime(
            dates.toOffsetDateTime(
                schedule.isIncrementBacklogStatus()
                    ? nextRun
                    : schedule.getNextIncrementScheduleTaskTime()));
      }
    }
    return info;
  }

  static BackupScheduleState toBackupScheduleState(Schedule.State state) {
    switch (state) {
      case Creating:
        return BackupScheduleState.BackupScheduleCreating;
      case Active:
        return BackupScheduleState.BackupScheduleActive;
      case Stopped:
      case Paused:
        return BackupScheduleState.BackupSchedulePaused;
      case Editing:
        return BackupScheduleState.BackupScheduleEditing;
      case Deleting:
        return BackupScheduleState.BackupScheduleDeleting;
      case Error:
        return BackupScheduleState.BackupScheduleError;
      default:
        throw new IllegalArgumentException("Unknown schedule state " + state);
    }
  }

  /** The v1 states a v2 state stands for. */
  static List<Schedule.State> toV1States(BackupScheduleState state) {
    switch (state) {
      case BackupScheduleCreating:
        return List.of(Schedule.State.Creating);
      case BackupScheduleActive:
        return List.of(Schedule.State.Active);
      case BackupSchedulePaused:
        return List.of(Schedule.State.Stopped, Schedule.State.Paused);
      case BackupScheduleEditing:
        return List.of(Schedule.State.Editing);
      case BackupScheduleDeleting:
        return List.of(Schedule.State.Deleting);
      case BackupScheduleError:
        return List.of(Schedule.State.Error);
      default:
        throw new IllegalArgumentException("Unknown backup schedule state " + state);
    }
  }

  static BackupScheduleTableType toBackupScheduleTableType(@Nullable String v1TableType) {
    if ("PGSQL_TABLE_TYPE".equals(v1TableType)) {
      return BackupScheduleTableType.BackupScheduleTableTypeYSQL;
    }
    if ("YQL_TABLE_TYPE".equals(v1TableType)) {
      return BackupScheduleTableType.BackupScheduleTableTypeYCQL;
    }
    return null;
  }

  default List<BackupScheduleKeyspace> toKeyspaces(JsonNode params) {
    List<BackupScheduleKeyspace> keyspaces = new ArrayList<>();
    JsonNode keyspaceTables = params.path("keyspaceTableList");
    if (keyspaceTables.isArray()) {
      for (JsonNode keyspaceTable : keyspaceTables) {
        keyspaces.add(toBackupScheduleKeyspace(Json.fromJson(keyspaceTable, KeyspaceTable.class)));
      }
    } else if (StringUtils.isNotEmpty(params.path("keyspace").asText(null))) {
      // BackupTableParams: one keyspace, with the tables in tableUUIDList or tableUUID.
      List<UUID> tables = new ArrayList<>();
      for (JsonNode table : params.path("tableUUIDList")) {
        tables.add(UUID.fromString(table.asText()));
      }
      UUID table = uuid(params, "tableUUID");
      if (tables.isEmpty() && table != null) {
        tables.add(table);
      }
      keyspaces.add(
          new BackupScheduleKeyspace()
              .keyspace(params.get("keyspace").asText())
              .tableUuids(tables.isEmpty() ? null : tables));
    }
    return keyspaces;
  }

  static BackupScheduleFrequency toFrequency(Schedule schedule) {
    if (StringUtils.isNotBlank(schedule.getCronExpression())) {
      return new BackupScheduleCronFrequency()
          .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyCron)
          .cronExpression(schedule.getCronExpression())
          .useLocalTimezone(schedule.isUseLocalTimezone());
    }
    return new BackupScheduleIntervalFrequency()
        .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyInterval)
        .interval(toDuration(schedule.getFrequency(), schedule.getFrequencyTimeUnit()));
  }

  /** The duration of {@code millis}, null when it isn't positive. */
  static BackupScheduleDuration toDuration(long millis, @Nullable TimeUnit unit) {
    if (millis <= 0) {
      return null;
    }
    TimeUnit chosen = null;
    if (unit != null && UNIT_MILLIS.containsKey(unit) && millis % UNIT_MILLIS.get(unit) == 0) {
      chosen = unit;
    } else {
      for (TimeUnit fallback : FALLBACK_UNITS) {
        if (millis % UNIT_MILLIS.get(fallback) == 0) {
          chosen = fallback;
          break;
        }
      }
    }
    return new BackupScheduleDuration()
        .amount(millis / UNIT_MILLIS.get(chosen))
        .unit(TimeUnitType.valueOf(chosen.name()));
  }

  private static TimeUnit timeUnit(JsonNode params, String field) {
    String unit = params.path(field).asText(null);
    if (unit == null) {
      return null;
    }
    try {
      return TimeUnit.valueOf(unit);
    } catch (IllegalArgumentException e) {
      return null;
    }
  }

  private static UUID uuid(JsonNode params, String field) {
    String uuid = params.path(field).asText(null);
    return StringUtils.isEmpty(uuid) ? null : UUID.fromString(uuid);
  }

  private static Boolean bool(JsonNode params, String field) {
    JsonNode value = params.get(field);
    return value == null || value.isNull() ? null : value.asBoolean();
  }
}
