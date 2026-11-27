// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import api.v2.handlers.BackupScheduleHandler;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.BackupScheduleApi;
import com.yugabyte.yba.v2.client.models.BackupSchedule;
import com.yugabyte.yba.v2.client.models.BackupScheduleApiFilter;
import com.yugabyte.yba.v2.client.models.BackupScheduleCronFrequency;
import com.yugabyte.yba.v2.client.models.BackupScheduleDuration;
import com.yugabyte.yba.v2.client.models.BackupScheduleFrequency;
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
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.ebean.DB;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

public class BackupScheduleApiTest extends FakeDBApplication {

  private static final long HOUR = 3600_000L;
  private static final long DAY = 24 * HOUR;

  private Customer customer;
  private BackupScheduleApi backupScheduleApi;

  private Universe universe;
  private CustomerConfig storageConfig;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe =
        ModelFactory.createUniverse(
            "backed-up",
            UUID.randomUUID(),
            customer.getId(),
            Common.CloudType.aws,
            null,
            null,
            true /* enableYbc */);
    storageConfig = ModelFactory.createS3StorageConfig(customer, "s3");
    when(mockCommissioner.submit(eq(TaskType.CreateBackupSchedule), any(ITaskParams.class)))
        // CustomerTask rows carry a foreign key onto task_info, so the task has to really exist.
        .thenAnswer(invocation -> buildTaskInfo(null, TaskType.CreateBackupSchedule));
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

  /** A daily YSQL schedule of the test universe, to the test storage config. */
  private BackupScheduleSpec createSpec(String name) {
    return new BackupScheduleSpec()
        .name(name)
        .universeUuid(universe.getUniverseUUID())
        .storageConfigUuid(storageConfig.getConfigUUID())
        .backupType(BackupScheduleTableType.BackupScheduleTableTypeYSQL)
        .frequency(
            new BackupScheduleFrequency(
                new BackupScheduleIntervalFrequency()
                    .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyInterval)
                    .interval(duration(1, TimeUnitType.DAYS))));
  }

  /** The params of the CreateBackupSchedule task submitted last. */
  private BackupScheduleTaskParams submittedCreate(int submits) {
    ArgumentCaptor<ITaskParams> params = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner, times(submits))
        .submit(eq(TaskType.CreateBackupSchedule), params.capture());
    return (BackupScheduleTaskParams) params.getValue();
  }

  private static JsonNode error(ApiException e) {
    return Json.parse(e.getResponseBody()).get("error");
  }

  @Test
  public void createSavesTheScheduleAndSubmitsTheTask() throws ApiException {
    UUID tableUUID = UUID.randomUUID();
    BackupScheduleSpec spec =
        createSpec("daily")
            .backupType(BackupScheduleTableType.BackupScheduleTableTypeYCQL)
            .keyspaces(
                List.of(new BackupScheduleKeyspace().keyspace("ks").tableUuids(List.of(tableUUID))))
            .incrementalInterval(duration(2, TimeUnitType.HOURS))
            .retention(duration(1, TimeUnitType.MONTHS))
            .minBackupsToRetain(5)
            .useTablespaces(true);

    YBATask task = backupScheduleApi.createBackupSchedule(customer.getUuid(), spec, null);

    // The schedule shows at once, in CREATING, with the spec it was created with.
    BackupSchedule got =
        backupScheduleApi.getBackupSchedule(customer.getUuid(), task.getResourceUuid());
    assertEquals(BackupScheduleState.BackupScheduleCreating, got.getInfo().getState());
    // Unset options come back with v1's defaults.
    assertEquals(
        spec.sse(false).tableByTableBackup(false).pointInTimeRestore(false).useRoles(false),
        got.getSpec());
    // The incremental interval is checked against the full backups' one, in milliseconds.
    verify(mockBackupHelper)
        .validateIncrementalScheduleFrequency(eq(2 * HOUR), eq(DAY), any(Universe.class));

    BackupScheduleTaskParams params = submittedCreate(1);
    assertEquals(task.getResourceUuid(), params.createdScheduleUUID);
    assertTrue(params.validateStorageConfigInTask);
    assertEquals(customer.getUuid(), params.getCustomerUUID());
    assertEquals(universe.getUniverseUUID(), params.getUniverseUUID());
    assertEquals(DAY, params.scheduleParams.schedulingFrequency);
    assertEquals(TimeUnit.DAYS, params.scheduleParams.frequencyTimeUnit);
    assertEquals(2 * HOUR, params.scheduleParams.incrementalBackupFrequency);
    assertEquals(30 * DAY, params.scheduleParams.timeBeforeDelete);
    assertEquals(TimeUnit.MONTHS, params.scheduleParams.expiryTimeUnit);
    CustomerTask customerTask = CustomerTask.findByTaskUUID(task.getTaskUuid());
    assertEquals(CustomerTask.TargetType.Schedule, customerTask.getTargetType());
    assertEquals(CustomerTask.TaskType.Create, customerTask.getType());
  }

  @Test
  public void createOnACron() throws ApiException {
    BackupScheduleSpec spec =
        createSpec("nightly")
            .frequency(
                new BackupScheduleFrequency(
                    new BackupScheduleCronFrequency()
                        .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyCron)
                        .cronExpression("0 2 * * *")
                        .useLocalTimezone(false)));

    YBATask task = backupScheduleApi.createBackupSchedule(customer.getUuid(), spec, null);

    Schedule schedule = Schedule.getOrBadRequest(customer.getUuid(), task.getResourceUuid());
    assertEquals("0 2 * * *", schedule.getCronExpression());
    assertFalse(schedule.isUseLocalTimezone());
    assertEquals(0, submittedCreate(1).scheduleParams.schedulingFrequency);
  }

  @Test
  public void createNameConflict() throws ApiException {
    YBATask first =
        backupScheduleApi.createBackupSchedule(customer.getUuid(), createSpec("x"), null);

    ApiException e =
        assertThrows(
            ApiException.class,
            () ->
                backupScheduleApi.createBackupSchedule(customer.getUuid(), createSpec("x"), null));
    assertEquals(409, e.getCode());
    assertEquals(BackupScheduleHandler.NAME_CONFLICT, error(e).get("code").asText());
    assertEquals(first.getResourceUuid().toString(), error(e).get("backup_schedule_uuid").asText());
    assertEquals("CREATING", error(e).get("backup_schedule_state").asText());
    submittedCreate(1);
  }

  @Test
  public void createMissingOrInactiveReferences() {
    assertCreateError(createSpec("a").universeUuid(UUID.randomUUID()), 404);
    // Another customer's universe.
    Customer other = ModelFactory.testCustomer("other-customer");
    assertCreateError(
        createSpec("a").universeUuid(ModelFactory.createUniverse(other.getId()).getUniverseUUID()),
        404);
    assertCreateError(createSpec("a").storageConfigUuid(UUID.randomUUID()), 404);
    assertCreateError(
        createSpec("a")
            .storageConfigUuid(ModelFactory.createS3StorageConfig(other, "other").getConfigUUID()),
        404);

    storageConfig.setState(CustomerConfig.ConfigState.QueuedForDeletion);
    storageConfig.save();
    ApiException e = assertCreateError(createSpec("a"), 409);
    assertEquals(BackupScheduleHandler.STORAGE_CONFIG_NOT_ACTIVE, error(e).get("code").asText());
    verify(mockCommissioner, never()).submit(any(TaskType.class), any(ITaskParams.class));
  }

  @Test
  public void createInvalidSpecs() {
    assertCreateError(
        createSpec("a").backupType(BackupScheduleTableType.BackupScheduleTableTypeREDIS), 400);
    assertCreateError(
        createSpec("a").keyspaces(List.of(new BackupScheduleKeyspace().tableUuids(List.of()))),
        400);
    // Shorter than v1's hour.
    assertCreateError(
        createSpec("a")
            .frequency(
                new BackupScheduleFrequency(
                    new BackupScheduleIntervalFrequency()
                        .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyInterval)
                        .interval(duration(30, TimeUnitType.MINUTES)))),
        400);
    assertCreateError(
        createSpec("a")
            .frequency(
                new BackupScheduleFrequency(
                    new BackupScheduleCronFrequency()
                        .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyCron)
                        .cronExpression("not a cron"))),
        400);
    // Not shorter than the full backups' interval. BackupHelper is a mock in these tests, so it is
    // made to reject the request the way the real one does.
    doThrow(new PlatformServiceException(400, "Incremental backup frequency should be lower"))
        .when(mockBackupHelper)
        .validateIncrementalScheduleFrequency(eq(DAY), eq(DAY), any(Universe.class));
    assertCreateError(createSpec("a").incrementalInterval(duration(1, TimeUnitType.DAYS)), 400);
    assertCreateError(createSpec("a").retention(duration(Long.MAX_VALUE, TimeUnitType.YEARS)), 400);
    assertCreateError(createSpec("a").retention(duration(5, TimeUnitType.NANOSECONDS)), 400);
    verify(mockCommissioner, never()).submit(any(TaskType.class), any(ITaskParams.class));
    assertEquals(
        0, Schedule.getAllByCustomerUUIDAndType(customer.getUuid(), TaskType.CreateBackup).size());
  }

  private ApiException assertCreateError(BackupScheduleSpec spec, int code) {
    ApiException e =
        assertThrows(
            ApiException.class,
            () -> backupScheduleApi.createBackupSchedule(customer.getUuid(), spec, null));
    assertEquals(e.getResponseBody(), code, e.getCode());
    return e;
  }

  @Test
  public void createIsIdempotent() throws ApiException {
    String key = UUID.randomUUID().toString();

    YBATask first =
        backupScheduleApi.createBackupSchedule(customer.getUuid(), createSpec("x"), key);
    YBATask retry =
        backupScheduleApi.createBackupSchedule(customer.getUuid(), createSpec("x"), key);

    assertEquals(first.getTaskUuid(), retry.getTaskUuid());
    assertEquals(first.getResourceUuid(), retry.getResourceUuid());
    submittedCreate(1);
  }

  @Test
  public void createRemovesTheScheduleWhenTheSubmitFails() {
    when(mockCommissioner.submit(eq(TaskType.CreateBackupSchedule), any(ITaskParams.class)))
        .thenThrow(new PlatformServiceException(400, "Universe is paused"));

    assertCreateError(createSpec("x"), 400);

    assertFalse(
        Schedule.maybeGetScheduleByUniverseWithName(
                "x", universe.getUniverseUUID(), customer.getUuid())
            .isPresent());
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
  public void reportsOperatorManagedSchedules() throws ApiException {
    // Created by the operator from a BackupSchedule custom resource: only the params say so.
    BackupRequestParams params = new BackupRequestParams();
    params.storageConfigUUID = UUID.randomUUID();
    params.setUniverseUUID(UUID.randomUUID());
    params.backupType = TableType.PGSQL_TABLE_TYPE;
    params.setKubernetesResourceDetails(new KubernetesResourceDetails("nightly", "yb"));
    Schedule fromResource =
        Schedule.create(customer.getUuid(), params, TaskType.CreateBackup, DAY, null);
    // Imported into the operator: the column is set.
    Schedule imported =
        createBackupSchedule(customer, "imported", UUID.randomUUID(), UUID.randomUUID());
    imported.setIsKubernetesOperatorControlled(true);
    Schedule plain = createBackupSchedule(customer, "plain", UUID.randomUUID(), UUID.randomUUID());

    assertTrue(operatorManaged(fromResource));
    assertTrue(operatorManaged(imported));
    assertFalse(operatorManaged(plain));
  }

  private boolean operatorManaged(Schedule schedule) throws ApiException {
    return backupScheduleApi
        .getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID())
        .getInfo()
        .getIsKubernetesOperatorControlled();
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
  }

  @Test
  public void getsAnOlderRedisSchedule() throws ApiException {
    Schedule schedule = createRedisSchedule();
    assertEquals(
        BackupScheduleTableType.BackupScheduleTableTypeREDIS,
        backupScheduleApi
            .getBackupSchedule(customer.getUuid(), schedule.getScheduleUUID())
            .getSpec()
            .getBackupType());
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
