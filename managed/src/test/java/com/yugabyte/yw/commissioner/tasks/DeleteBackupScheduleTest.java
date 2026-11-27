// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class DeleteBackupScheduleTest extends CommissionerBaseTest {

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
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams params = universe.getUniverseDetails();
          params.setYbcInstalled(true);
          params.setEnableYbc(true);
        });
    ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 3);
    defaultUser = ModelFactory.testUser(defaultCustomer);
    storageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "S3-config");
  }

  /** Submits the delete task with a universe lock that fails. */
  private TaskInfo submitTask(BackupRequestParams scheduleParams, UUID scheduleUUID) {
    try {
      BackupScheduleTaskParams params =
          UniverseControllerRequestBinder.deepCopy(
              Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails(),
              BackupScheduleTaskParams.class);
      params.scheduleParams = scheduleParams;
      params.customerUUID = defaultCustomer.getUuid();
      params.scheduleUUID = scheduleUUID;
      // The universe lock throws before it locks anything for a prechecks-only run. A universe
      // that's already locked can't stand in: the submit itself rejects it.
      params.setRunOnlyPrechecks(true);
      UUID taskUUID = commissioner.submit(TaskType.DeleteBackupSchedule, params);
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Schedule,
          CustomerTask.TaskType.Delete,
          defaultUniverse.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testDeleteReleasesTheScheduleWhenTheUniverseLockFails() {
    BackupRequestParams backupParams = new BackupRequestParams();
    backupParams.schedulingFrequency = 100000000L;
    backupParams.frequencyTimeUnit = TimeUnit.HOURS;
    backupParams.incrementalBackupFrequency = 86200000L;
    backupParams.incrementalBackupFrequencyTimeUnit = TimeUnit.MINUTES;
    backupParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupParams.enablePointInTimeRestore = true;
    backupParams.scheduleName = "test-schedule";
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupParams, TaskType.CreateBackup, 100000000L, null);
    UUID scheduleUUID = schedule.getScheduleUUID();

    TaskInfo tInfo =
        submitTask(
            Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class), scheduleUUID);

    assertEquals(TaskInfo.State.Failure, tInfo.getTaskState());
    schedule = Schedule.getOrBadRequest(defaultCustomer.getUuid(), scheduleUUID);
    // Untouched and released, so that a later delete can run.
    assertEquals(State.Active, schedule.getStatus());
    assertFalse(schedule.isRunningState());
  }
}
