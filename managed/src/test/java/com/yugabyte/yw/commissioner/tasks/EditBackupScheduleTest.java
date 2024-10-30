package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleEditParams;
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
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class EditBackupScheduleTest extends CommissionerBaseTest {

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

  private TaskInfo submitTask(BackupRequestParams scheduleParams, UUID scheduleUUID) {
    try {
      BackupScheduleTaskParams params =
          UniverseControllerRequestBinder.deepCopy(
              defaultUniverse.getUniverseDetails(), BackupScheduleTaskParams.class);
      params.scheduleParams = scheduleParams;
      params.customerUUID = defaultCustomer.getUuid();
      params.scheduleUUID = scheduleUUID;
      UUID taskUUID = commissioner.submit(TaskType.EditBackupSchedule, params);
      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Schedule,
          CustomerTask.TaskType.Update,
          defaultUniverse.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private Schedule createSchedule(
      long frequency, long incrementalBackupFrequency, String cron, boolean enablePITRestore) {
    BackupRequestParams backupParams = new BackupRequestParams();
    backupParams.schedulingFrequency = frequency;
    backupParams.frequencyTimeUnit = TimeUnit.HOURS;
    backupParams.incrementalBackupFrequency = incrementalBackupFrequency;
    backupParams.incrementalBackupFrequencyTimeUnit = TimeUnit.MINUTES;
    backupParams.cronExpression = cron;
    backupParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupParams.enablePointInTimeRestore = enablePITRestore;
    backupParams.scheduleName = "test-schedule";
    return Schedule.create(
        defaultCustomer.getUuid(), backupParams, TaskType.CreateBackup, frequency, cron);
  }

  @Test
  public void testEditBackupScheduleNoTimeUnitFail() {
    Schedule schedule =
        createSchedule(100000000L, 5000000L, null /* cron */, false /* enablePITRestore */);
    UUID scheduleUUID = schedule.getScheduleUUID();
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    BackupScheduleEditParams editParams = new BackupScheduleEditParams(scheduleParams);
    editParams.schedulingFrequency = 150000000L;
    editParams.frequencyTimeUnit = null;
    scheduleParams.applyScheduleEditParams(editParams);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> submitTask(scheduleParams, scheduleUUID));
    assertEquals(ex.getMessage(), "Frequency time unit cannot be null");
  }

  @Test
  public void testEditBackupScheduleAddIncrFrequencyToNonIncrScheduleFail() {
    Schedule schedule =
        createSchedule(100000000L, 0L, null /* cron */, false /* enablePITRestore */);
    UUID scheduleUUID = schedule.getScheduleUUID();
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    BackupScheduleEditParams editParams = new BackupScheduleEditParams(scheduleParams);
    editParams.incrementalBackupFrequency = 864000L;
    editParams.incrementalBackupFrequencyTimeUnit = TimeUnit.MINUTES;
    scheduleParams.applyScheduleEditParams(editParams);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> submitTask(scheduleParams, scheduleUUID));
    assertEquals(
        ex.getMessage(),
        "Schedule does not have Incremental backups enabled, cannot provide Incremental backup"
            + " frequency");
  }

  @Test
  public void testEditBackupScheduleEditIncrFrequencyMissingForIncrScheduleFail() {
    Schedule schedule =
        createSchedule(100000000L, 86400000L, null /* cron */, false /* enablePITRestore */);
    UUID scheduleUUID = schedule.getScheduleUUID();
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    BackupScheduleEditParams editParams = new BackupScheduleEditParams(scheduleParams);
    editParams.incrementalBackupFrequency = 0L;
    scheduleParams.applyScheduleEditParams(editParams);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> submitTask(scheduleParams, scheduleUUID));
    assertEquals(
        ex.getMessage(),
        "Incremental backup frequency required for Incremental backup enabled schedules");
  }

  @Test
  public void testEditBackupSchedulePITRetentionMoreThanAcceptableFail() {
    Schedule schedule =
        createSchedule(100000000L, 86400000L, null /* cron */, true /* enablePITRestore */);
    UUID scheduleUUID = schedule.getScheduleUUID();
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    BackupScheduleEditParams editParams = new BackupScheduleEditParams(scheduleParams);
    editParams.incrementalBackupFrequency = 86500000L;
    editParams.incrementalBackupFrequencyTimeUnit = TimeUnit.MINUTES;
    scheduleParams.applyScheduleEditParams(editParams);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> submitTask(scheduleParams, scheduleUUID));
    assertEquals(
        ex.getMessage(),
        "Cannot have history retention more than 24 hrs, please use more frequent backups");
  }

  @Test
  public void testEditBackupScheduleWithPITRetentionSuccess() {
    Schedule schedule =
        createSchedule(100000000L, 86200000L, null /* cron */, true /* enablePITRestore */);
    UUID scheduleUUID = schedule.getScheduleUUID();
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    BackupScheduleEditParams editParams = new BackupScheduleEditParams(scheduleParams);
    editParams.incrementalBackupFrequency = 86400000L;
    editParams.incrementalBackupFrequencyTimeUnit = TimeUnit.MINUTES;
    scheduleParams.applyScheduleEditParams(editParams);
    TaskInfo tInfo = submitTask(scheduleParams, scheduleUUID);
    schedule = Schedule.getOrBadRequest(defaultCustomer.getUuid(), scheduleUUID);
    scheduleParams = Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    assertEquals(86400000L /* modified */, scheduleParams.incrementalBackupFrequency);
    List<TaskInfo> subTasks = tInfo.getSubTasks();
    List<TaskType> taskTypes =
        subTasks.stream().map(sT -> sT.getTaskType()).collect(Collectors.toList());
    assertTrue(taskTypes.contains(TaskType.SetFlagInMemory));
    assertTrue(taskTypes.contains(TaskType.AnsibleConfigureServers));
  }
}
