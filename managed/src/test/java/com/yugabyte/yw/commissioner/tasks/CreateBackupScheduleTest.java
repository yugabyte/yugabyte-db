package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

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
  private Universe defaultUniverse;
  private CustomerConfig storageConfig;
  private Users defaultUser;

  @Override
  @Before
  public void setUp() {
    super.setUp();
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
    try {
      BackupScheduleTaskParams params =
          UniverseControllerRequestBinder.deepCopy(
              defaultUniverse.getUniverseDetails(), BackupScheduleTaskParams.class);
      params.scheduleParams = scheduleParams;
      params.customerUUID = defaultCustomer.getUuid();
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
        params.frequencyTimeUnit,
        params.scheduleName);
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitTask(params));
    assertEquals("Schedule with same name already exists", ex.getMessage());
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
}
