// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.BackupScheduleMapper;
import api.v2.models.BackupSchedule;
import api.v2.models.BackupScheduleApiFilter;
import api.v2.models.BackupSchedulePagedQuerySpec;
import api.v2.models.BackupSchedulePagedResp;
import api.v2.models.BackupScheduleState;
import api.v2.utils.NormalizedPaginationSpec;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.models.Schedule;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.PagedList;
import io.ebean.SqlRow;
import java.util.Collection;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.yb.CommonTypes.TableType;

/** v2 backup schedules: the v1 schedules whose task is a backup. */
@Singleton
public class BackupScheduleHandler {

  private static final String BACKUP_TYPE_PARAM = "task_params->>'backupType'";
  private static final List<String> V2_BACKUP_TYPES =
      List.of(
          TableType.PGSQL_TABLE_TYPE.name(),
          TableType.YQL_TABLE_TYPE.name(),
          TableType.REDIS_TABLE_TYPE.name());

  public BackupSchedule getBackupSchedule(UUID cUUID, UUID bsUUID) {
    Schedule schedule = backupSchedules(cUUID).idEq(bsUUID).findOne();
    if (schedule == null) {
      throw new PlatformServiceException(NOT_FOUND, "No backup schedule with UUID " + bsUUID);
    }
    return toBackupSchedule(schedule, lastTasks(List.of(bsUUID)));
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
