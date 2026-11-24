// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.BackupScheduleApi;
import com.yugabyte.yba.v2.client.models.BackupSchedule;
import com.yugabyte.yba.v2.client.models.BackupScheduleApiFilter;
import com.yugabyte.yba.v2.client.models.BackupScheduleCronFrequency;
import com.yugabyte.yba.v2.client.models.BackupScheduleDuration;
import com.yugabyte.yba.v2.client.models.BackupScheduleFrequencyType;
import com.yugabyte.yba.v2.client.models.BackupScheduleInfo;
import com.yugabyte.yba.v2.client.models.BackupScheduleIntervalFrequency;
import com.yugabyte.yba.v2.client.models.BackupScheduleKeyspace;
import com.yugabyte.yba.v2.client.models.BackupSchedulePagedQuerySpec;
import com.yugabyte.yba.v2.client.models.BackupSchedulePagedResp;
import com.yugabyte.yba.v2.client.models.BackupScheduleSpec;
import com.yugabyte.yba.v2.client.models.BackupScheduleState;
import com.yugabyte.yba.v2.client.models.BackupScheduleTableType;
import com.yugabyte.yba.v2.client.models.TimeUnitType;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.ebean.DB;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

public class BackupScheduleApiTest extends FakeDBApplication {

  private static final long HOUR = 3600_000L;
  private static final long DAY = 24 * HOUR;

  private Customer customer;
  private BackupScheduleApi backupScheduleApi;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    ApiClient v2Client = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2Client =
        v2Client.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", user.createAuthToken());
    Configuration.setDefaultApiClient(v2Client);
    backupScheduleApi = new BackupScheduleApi();
  }

  /** A CreateBackup schedule, as the current v1 create makes them. */
  private Schedule createBackupSchedule(
      Customer owner, String name, UUID universeUUID, UUID storageConfigUUID) {
    BackupRequestParams params = new BackupRequestParams();
    params.storageConfigUUID = storageConfigUUID;
    params.setUniverseUUID(universeUUID);
    params.backupType = TableType.PGSQL_TABLE_TYPE;
    params.keyspaceTableList = new ArrayList<>();
    return Schedule.create(
        owner.getUuid(),
        universeUUID,
        params,
        TaskType.CreateBackup,
        DAY,
        null,
        true,
        TimeUnit.DAYS,
        name);
  }

  @Test
  public void getsACreateBackupSchedule() throws ApiException {
    UUID universeUUID = UUID.randomUUID();
    UUID storageConfigUUID = UUID.randomUUID();
    UUID kmsConfigUUID = UUID.randomUUID();
    UUID tableUUID = UUID.randomUUID();
    BackupRequestParams params = new BackupRequestParams();
    params.storageConfigUUID = storageConfigUUID;
    params.setUniverseUUID(universeUUID);
    params.backupType = TableType.YQL_TABLE_TYPE;
    KeyspaceTable keyspaceTable = new KeyspaceTable();
    keyspaceTable.keyspace = "ks";
    keyspaceTable.tableUUIDList = List.of(tableUUID);
    params.keyspaceTableList = List.of(keyspaceTable);
    params.incrementalBackupFrequency = HOUR;
    params.incrementalBackupFrequencyTimeUnit = TimeUnit.HOURS;
    params.timeBeforeDelete = 7 * DAY;
    params.expiryTimeUnit = TimeUnit.DAYS;
    params.minNumBackupsToRetain = 3;
    params.kmsConfigUUID = kmsConfigUUID;
    params.sse = true;
    params.tableByTableBackup = true;
    params.enablePointInTimeRestore = true;
    params.setUseRoles(true);
    Schedule schedule =
        Schedule.create(
            customer.getUuid(),
            universeUUID,
            params,
            TaskType.CreateBackup,
            2 * DAY,
            null,
            true,
            TimeUnit.DAYS,
            "daily");
    ScheduleTask.create(UUID.randomUUID(), schedule.getScheduleUUID()).markCompleted();
    ScheduleTask last = ScheduleTask.create(UUID.randomUUID(), schedule.getScheduleUUID());
    Date lastTime = new Date(System.currentTimeMillis() + 1000);
    last.setScheduledTime(lastTime);
    last.save();

    BackupSchedule got =
        backupScheduleApi.getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID());

    BackupScheduleSpec spec = got.getSpec();
    assertEquals("daily", spec.getName());
    assertEquals(universeUUID, spec.getUniverseUuid());
    assertEquals(storageConfigUUID, spec.getStorageConfigUuid());
    assertEquals(BackupScheduleTableType.BackupScheduleTableTypeYCQL, spec.getBackupType());
    assertEquals(
        List.of(new BackupScheduleKeyspace().keyspace("ks").tableUuids(List.of(tableUUID))),
        spec.getKeyspaces());
    BackupScheduleIntervalFrequency frequency =
        (BackupScheduleIntervalFrequency) spec.getFrequency().getActualInstance();
    assertEquals(
        BackupScheduleFrequencyType.BackupScheduleFrequencyInterval, frequency.getFrequencyType());
    assertEquals(duration(2, TimeUnitType.DAYS), frequency.getInterval());
    assertEquals(duration(1, TimeUnitType.HOURS), spec.getIncrementalInterval());
    assertEquals(duration(7, TimeUnitType.DAYS), spec.getRetention());
    assertEquals(Integer.valueOf(3), spec.getMinBackupsToRetain());
    assertEquals(kmsConfigUUID, spec.getKmsConfigUuid());
    assertTrue(spec.getSse());
    assertTrue(spec.getTableByTableBackup());
    assertTrue(spec.getPointInTimeRestore());
    assertTrue(spec.getUseRoles());
    assertFalse(spec.getUseTablespaces());

    BackupScheduleInfo info = got.getInfo();
    assertEquals(schedule.getScheduleUUID(), info.getUuid());
    assertEquals(BackupScheduleState.BackupScheduleActive, info.getState());
    // The last task hasn't completed.
    assertTrue(info.getBackupRunning());
    assertNotNull(info.getNextBackupTime());
    assertNotNull(info.getNextIncrementalBackupTime());
    assertEquals(lastTime.toInstant(), info.getLastBackupTime().toInstant());
    assertFalse(info.getIsKubernetesOperatorControlled());

    last.markCompleted();
    assertFalse(
        backupScheduleApi
            .getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID())
            .getInfo()
            .getBackupRunning());
  }

  @Test
  public void fillsWhatOlderParamsAndColumnsLack() throws ApiException {
    Schedule schedule =
        createBackupSchedule(customer, "incremental", UUID.randomUUID(), UUID.randomUUID());
    BackupRequestParams params = Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    params.incrementalBackupFrequency = HOUR;
    schedule.setTaskParams(Json.toJson(params));
    schedule.save();
    // Params saved before minNumBackupsToRetain existed, and a schedule from before the next
    // incremental time was stored.
    DB.sqlUpdate(
            "update schedule set task_params = (task_params::jsonb -"
                + " 'minNumBackupsToRetain')::json, next_increment_schedule_task_time = null where"
                + " schedule_uuid = :uuid")
        .setParameter("uuid", schedule.getScheduleUUID())
        .execute();

    BackupSchedule got =
        backupScheduleApi.getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID());

    // v1 runs it with the default.
    assertEquals(Integer.valueOf(3), got.getSpec().getMinBackupsToRetain());
    // The time the scheduler stores on its next run.
    assertNotNull(got.getInfo().getNextIncrementalBackupTime());
    assertFalse(got.getInfo().getBackupRunning());
  }

  @Test
  public void getsAMultiTableBackupScheduleOfTablesWithoutAKeyspace() throws ApiException {
    UUID universeUUID = UUID.randomUUID();
    List<UUID> tables = List.of(UUID.randomUUID(), UUID.randomUUID());
    MultiTableBackup.Params params = new MultiTableBackup.Params();
    params.storageConfigUUID = UUID.randomUUID();
    params.setUniverseUUID(universeUUID);
    params.backupType = TableType.YQL_TABLE_TYPE;
    params.tableUUIDList = tables;
    Schedule schedule =
        Schedule.create(
            customer.getUuid(),
            universeUUID,
            params,
            TaskType.MultiTableBackup,
            DAY,
            null,
            true,
            TimeUnit.DAYS,
            "tables");

    BackupSchedule got =
        backupScheduleApi.getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID());

    // Only those tables, not every keyspace.
    assertEquals(
        List.of(new BackupScheduleKeyspace().tableUuids(tables)), got.getSpec().getKeyspaces());
  }

  private static BackupScheduleDuration duration(long amount, TimeUnitType unit) {
    return new BackupScheduleDuration().amount(amount).unit(unit);
  }

  @Test
  public void getsAnOlderBackupUniverseScheduleOnACron() throws ApiException {
    UUID universeUUID = UUID.randomUUID();
    UUID tableUUID = UUID.randomUUID();
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = UUID.randomUUID();
    params.setUniverseUUID(universeUUID);
    params.setKeyspace("foo");
    params.backupType = TableType.PGSQL_TABLE_TYPE;
    params.tableUUID = tableUUID;
    Schedule schedule =
        Schedule.create(
            customer.getUuid(),
            universeUUID,
            params,
            TaskType.BackupUniverse,
            0,
            "0 2 * * *",
            false,
            TimeUnit.MINUTES,
            "nightly");
    schedule.setStatus(Schedule.State.Stopped);
    schedule.save();

    BackupSchedule got =
        backupScheduleApi.getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID());

    BackupScheduleSpec spec = got.getSpec();
    assertEquals(BackupScheduleTableType.BackupScheduleTableTypeYSQL, spec.getBackupType());
    assertEquals(
        List.of(new BackupScheduleKeyspace().keyspace("foo").tableUuids(List.of(tableUUID))),
        spec.getKeyspaces());
    BackupScheduleCronFrequency cron =
        (BackupScheduleCronFrequency) spec.getFrequency().getActualInstance();
    assertEquals(BackupScheduleFrequencyType.BackupScheduleFrequencyCron, cron.getFrequencyType());
    assertEquals("0 2 * * *", cron.getCronExpression());
    assertFalse(cron.getUseLocalTimezone());
    assertNull(spec.getIncrementalInterval());
    assertNull(spec.getRetention());
    // Older params carry the v1 default.
    assertEquals(Integer.valueOf(3), spec.getMinBackupsToRetain());
    // A paused (v1 Stopped) schedule has no due times.
    assertEquals(BackupScheduleState.BackupSchedulePaused, got.getInfo().getState());
    assertNull(got.getInfo().getNextBackupTime());
    assertNull(got.getInfo().getLastBackupTime());
  }

  @Test
  public void getMissing() {
    assertEquals(404, getError(UUID.randomUUID()));

    // A schedule that runs something else isn't a backup schedule.
    Schedule script =
        Schedule.create(
            customer.getUuid(),
            UUID.randomUUID(),
            new BackupTableParams(),
            TaskType.ExternalScript,
            HOUR,
            null,
            true,
            TimeUnit.HOURS,
            "script");
    assertEquals(404, getError(script.getScheduleUUID()));

    // Nor is another customer's.
    Customer other = ModelFactory.testCustomer("other-customer");
    Schedule otherSchedule =
        createBackupSchedule(other, "other", UUID.randomUUID(), UUID.randomUUID());
    assertEquals(404, getError(otherSchedule.getScheduleUUID()));

    // Nor a Redis one, which v2 has no backup type for.
    assertEquals(404, getError(createRedisSchedule().getScheduleUUID()));
  }

  private Schedule createRedisSchedule() {
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = UUID.randomUUID();
    params.setUniverseUUID(UUID.randomUUID());
    params.backupType = TableType.REDIS_TABLE_TYPE;
    return Schedule.create(customer.getUuid(), params, TaskType.MultiTableBackup, 1000, null);
  }

  private int getError(UUID bsUUID) {
    return assertThrows(
            ApiException.class,
            () -> backupScheduleApi.getBackupSchedule(customer.getUuid(), bsUUID))
        .getCode();
  }

  private List<String> list(BackupScheduleApiFilter filter) throws ApiException {
    BackupSchedulePagedQuerySpec query = new BackupSchedulePagedQuerySpec();
    query.setFilter(filter);
    return names(backupScheduleApi.pageListBackupSchedules(customer.getUuid(), query));
  }

  private static List<String> names(BackupSchedulePagedResp page) {
    return page.getEntities().stream().map(s -> s.getSpec().getName()).toList();
  }

  @Test
  public void listWithFilters() throws ApiException {
    UUID universe1 = UUID.randomUUID();
    UUID universe2 = UUID.randomUUID();
    UUID config1 = UUID.randomUUID();
    UUID config2 = UUID.randomUUID();
    createBackupSchedule(customer, "b-daily", universe1, config1);
    Schedule paused = createBackupSchedule(customer, "A-weekly", universe1, config2);
    paused.setStatus(Schedule.State.Stopped);
    paused.save();
    Schedule failed = createBackupSchedule(customer, "c-failed", universe2, config1);
    failed.setStatus(Schedule.State.Error);
    failed.save();
    // v1's older schedules count too.
    ModelFactory.createScheduleBackup(customer.getUuid(), universe2, config2);
    Schedule.create(
        customer.getUuid(),
        universe1,
        new BackupTableParams(),
        TaskType.ExternalScript,
        HOUR,
        null,
        true,
        TimeUnit.HOURS,
        "script");
    createBackupSchedule(ModelFactory.testCustomer("other-customer"), "other", universe1, config1);
    createRedisSchedule();

    List<String> all = list(null);
    assertEquals(4, all.size());
    // By name, case-insensitive; the older schedule's generated name starts with "schedule-".
    assertEquals(List.of("A-weekly", "b-daily", "c-failed"), all.subList(0, 3));
    assertTrue(all.get(3).startsWith("schedule-"));

    assertEquals(List.of("b-daily"), list(new BackupScheduleApiFilter().name("b-daily")));
    assertEquals(
        List.of("A-weekly", "b-daily"),
        list(new BackupScheduleApiFilter().universeUuids(List.of(universe1))));
    // The storage config is matched in the task params, of both v1 shapes.
    List<String> withConfig2 =
        list(new BackupScheduleApiFilter().storageConfigUuids(List.of(config2)));
    assertEquals(2, withConfig2.size());
    assertEquals("A-weekly", withConfig2.get(0));
    assertEquals(
        List.of("b-daily", "c-failed"),
        list(new BackupScheduleApiFilter().storageConfigUuids(List.of(config1))));
    assertEquals(
        List.of("A-weekly"),
        list(
            new BackupScheduleApiFilter()
                .states(List.of(BackupScheduleState.BackupSchedulePaused))));
    assertEquals(
        List.of("c-failed"),
        list(
            new BackupScheduleApiFilter()
                .states(List.of(BackupScheduleState.BackupScheduleError))
                .storageConfigUuids(List.of(config1))));
    assertEquals(
        List.of(),
        list(new BackupScheduleApiFilter().storageConfigUuids(List.of(UUID.randomUUID()))));

    BackupSchedulePagedQuerySpec page = new BackupSchedulePagedQuerySpec();
    page.setLimit(2);
    page.setOffset(1);
    page.setDirection(BackupSchedulePagedQuerySpec.DirectionEnum.DESC);
    BackupSchedulePagedResp resp =
        backupScheduleApi.pageListBackupSchedules(customer.getUuid(), page);
    assertEquals(List.of("c-failed", "b-daily"), names(resp));
    assertEquals(Integer.valueOf(4), resp.getTotalCount());
    assertTrue(resp.getHasNext());
    assertTrue(resp.getHasPrev());
  }
}
