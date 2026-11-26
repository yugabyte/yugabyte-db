package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doThrow;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

public class CreateBackupScheduleTest extends CommissionerBaseTest {
  private static final long DAY = 24 * 3600_000L;

  private Universe defaultUniverse;
  private CustomerConfig storageConfig;
  private Users defaultUser;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse =
        ModelFactory.createUniverse(
            "test-universe",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            Common.CloudType.aws,
            null,
            null);
    Universe.UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams params = universe.getUniverseDetails();
            params.setYbcInstalled(true);
            params.setEnableYbc(true);
          }
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 3);
    defaultUser = ModelFactory.testUser(defaultCustomer);
    storageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "S3-config");
  }

  private TaskInfo submitTask(BackupRequestParams scheduleParams) {
    return submitTask(scheduleParams, null);
  }

  /** {@code createdScheduleUUID}: the schedule a v2 create saved, or null for v1. */
  private TaskInfo submitTask(BackupRequestParams scheduleParams, UUID createdScheduleUUID) {
    try {
      BackupScheduleTaskParams params =
          UniverseControllerRequestBinder.deepCopy(
              defaultUniverse.getUniverseDetails(), BackupScheduleTaskParams.class);
      params.scheduleParams = scheduleParams;
      params.customerUUID = defaultCustomer.getUuid();
      if (createdScheduleUUID != null) {
        params.createdScheduleUUID = createdScheduleUUID;
        params.validateStorageConfigInTask = true;
      }
      UUID taskUUID = commissioner.submit(TaskType.CreateBackupSchedule, params);
      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Schedule,
          CustomerTask.TaskType.Create,
          defaultUniverse.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private BackupRequestParams createScheduleBackupParams(
      long frequency,
      TimeUnit frequencyTimeUnit,
      long incrementalBackupFrequency,
      TimeUnit incrementalBackupFrequencyTimeUnit,
      String cron,
      boolean enablePITRestore,
      String scheduleName) {
    BackupRequestParams backupParams = new BackupRequestParams();
    backupParams.schedulingFrequency = frequency;
    backupParams.frequencyTimeUnit = frequencyTimeUnit;
    backupParams.incrementalBackupFrequency = incrementalBackupFrequency;
    backupParams.incrementalBackupFrequencyTimeUnit = incrementalBackupFrequencyTimeUnit;
    backupParams.cronExpression = cron;
    backupParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupParams.enablePointInTimeRestore = enablePITRestore;
    backupParams.scheduleName = scheduleName;
    backupParams.backupType = TableType.PGSQL_TABLE_TYPE;
    return backupParams;
  }

  @Test
  public void testCreateScheduleBackupAsyncWithoutNameFail() {
    BackupRequestParams params =
        createScheduleBackupParams(0L, null, 0L, null, "0 */2 * * *", false, null);
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals("Schedule name cannot be empty", ex.getMessage());
  }

  @Test
  public void testCreateScheduleBackupAsyncWithoutTimeUnitFail() {
    BackupRequestParams params =
        createScheduleBackupParams(10000000L, null, 0L, null, null, false, "test");
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals("Frequency time unit cannot be null", ex.getMessage());
  }

  @Test
  public void testCreateScheduleBackupAsyncWithDuplicateNameFail() {
    BackupRequestParams params =
        createScheduleBackupParams(10000000L, TimeUnit.HOURS, 0L, null, null, false, "test");
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        params,
        TaskType.CreateBackup,
        params.schedulingFrequency,
        null,
        true /* useLocalTimezone */,
        params.frequencyTimeUnit,
        params.scheduleName);
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals("Schedule with same name test already exists", ex.getMessage());
  }

  @Test
  public void testCreateScheduledBackupAsyncNoCronOrFrequencyFail() {
    BackupRequestParams params =
        createScheduleBackupParams(0L, null, 0L, null, null, false, "test");
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals(
        "Provide atleast one of scheduling frequency and cron expression", ex.getMessage());
  }

  @Test
  public void testCreateIncrementalScheduleBackupAsyncWithoutIncrFrequencyTimeUnitFail() {
    BackupRequestParams params =
        createScheduleBackupParams(10000000L, TimeUnit.HOURS, 1000000L, null, null, false, "test");
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals("Incremental backup frequency time unit cannot be null", ex.getMessage());
  }

  @Test
  public void testCreateIncrementalScheduleBackupAsyncWithBaseBackupFail() {
    BackupRequestParams params =
        createScheduleBackupParams(
            10000000L, TimeUnit.HOURS, 1000000L, TimeUnit.MINUTES, null, false, "test");
    params.baseBackupUUID = UUID.randomUUID();
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals("Cannot assign base backup in scheduled backups", ex.getMessage());
  }

  @Test
  public void testV2CreateWhoseStorageCheckFailsRemovesTheSchedule() {
    BackupRequestParams params =
        createScheduleBackupParams(DAY, TimeUnit.DAYS, 0L, null, null, false, "v2-schedule");
    params.customerUUID = defaultCustomer.getUuid();
    // As the v2 create saves it.
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            params,
            TaskType.CreateBackup,
            params.schedulingFrequency,
            null,
            true /* useLocalTimezone */,
            params.frequencyTimeUnit,
            params.scheduleName,
            Schedule.State.Creating);
    doThrow(new PlatformServiceException(BAD_REQUEST, "The bucket can't be reached"))
        .when(mockBackupHelper)
        .validateStorageConfig(any());

    TaskInfo taskInfo = submitTask(params, schedule.getScheduleUUID());

    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    // The caller learns why from the task: its failed precheck subtask.
    assertTrue(
        taskInfo.getErrorMessage(),
        taskInfo.getErrorMessage().contains("BackupStorageConfigValidate"));
    TaskInfo precheck =
        taskInfo.getSubTasks().stream()
            .filter(subTask -> subTask.getTaskType() == TaskType.BackupStorageConfigValidate)
            .findFirst()
            .orElseThrow();
    assertEquals(TaskInfo.State.Failure, precheck.getTaskState());
    assertTrue(precheck.getErrorMessage(), precheck.getErrorMessage().contains("can't be reached"));
    // And the name is free for a retry.
    assertFalse(Schedule.maybeGet(schedule.getScheduleUUID()).isPresent());
    // The universe isn't left locked.
    assertFalse(
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .updateInProgress);
  }

  @Test
  public void testV2CreateWhoseSubmitCheckFailsRemovesTheSchedule() {
    // No frequency time unit: the check when the task is submitted fails.
    BackupRequestParams params =
        createScheduleBackupParams(DAY, null, 0L, null, null, false, "v2-schedule");
    params.customerUUID = defaultCustomer.getUuid();
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            params,
            TaskType.CreateBackup,
            params.schedulingFrequency,
            null,
            true /* useLocalTimezone */,
            TimeUnit.DAYS,
            params.scheduleName,
            Schedule.State.Creating);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> submitTask(params, schedule.getScheduleUUID()));

    assertEquals("Frequency time unit cannot be null", ex.getMessage());
    assertFalse(Schedule.maybeGet(schedule.getScheduleUUID()).isPresent());
  }
}
