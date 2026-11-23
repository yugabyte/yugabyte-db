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

/** v2 backup schedules: the v1 schedules whose task is a backup. */
@Singleton
public class BackupScheduleHandler {

  public BackupSchedule getBackupSchedule(UUID cUUID, UUID bsUUID) {
    Schedule schedule = backupSchedules(cUUID).idEq(bsUUID).findOne();
    if (schedule == null) {
      throw new PlatformServiceException(NOT_FOUND, "No backup schedule with UUID " + bsUUID);
    }
    return BackupScheduleMapper.INSTANCE.toBackupSchedule(
        schedule, lastBackupTimes(List.of(bsUUID)).get(bsUUID));
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
    Map<UUID, Date> lastBackupTimes =
        lastBackupTimes(
            page.getList().stream().map(Schedule::getScheduleUUID).collect(Collectors.toList()));
    return HandlerPagingSupport.pagedResponse(
        new BackupSchedulePagedResp(),
        page,
        schedule ->
            BackupScheduleMapper.INSTANCE.toBackupSchedule(
                schedule, lastBackupTimes.get(schedule.getScheduleUUID())));
  }

  private static ExpressionList<Schedule> backupSchedules(UUID cUUID) {
    return Schedule.find
        .query()
        .where()
        .eq("customer_uuid", cUUID)
        .in("task_type", BackupUtil.BACKUP_TASK_TYPES);
  }

  private static List<Schedule.State> toV1States(Collection<BackupScheduleState> states) {
    return states.stream()
        .flatMap(state -> BackupScheduleMapper.toV1States(state).stream())
        .distinct()
        .collect(Collectors.toList());
  }

  /** When each schedule last started a backup, in one query. */
  private static Map<UUID, Date> lastBackupTimes(List<UUID> scheduleUUIDs) {
    Map<UUID, Date> times = new HashMap<>();
    if (scheduleUUIDs.isEmpty()) {
      return times;
    }
    List<SqlRow> rows =
        DB.sqlQuery(
                "select schedule_uuid, max(scheduled_time) as last_time from schedule_task"
                    + " where schedule_uuid in (:uuids) group by schedule_uuid")
            .setParameter("uuids", scheduleUUIDs)
            .findList();
    for (SqlRow row : rows) {
      Date lastTime = row.getUtilDate("last_time");
      if (lastTime != null) {
        times.put(row.getUUID("schedule_uuid"), lastTime);
      }
    }
    return times;
  }
}
