// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.time.Duration;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class ScheduleUtilTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private Customer defaultCustomer;
  private CustomerConfig customerConfig;
  private BackupRequestParams backupRequestParams;
  private Users defaultUser;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUser = ModelFactory.testUser(defaultCustomer);

    backupRequestParams = new BackupRequestParams();
    backupRequestParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST16");
    backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();
  }

  @Test
  public void testDetectIncrementalBackup() {
    // Set http context
    TestUtils.setFakeHttpContext(defaultUser);
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupRequestParams, TaskType.CreateBackup, 1000, null);
    assertFalse(ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID()));
    backupRequestParams.incrementalBackupFrequency = 100000L;
    schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupRequestParams, TaskType.CreateBackup, 1000, null);
    assertTrue(ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID()));
  }

  @Test
  public void testGetBackupIntervalForPITRestoreSecs() {
    backupRequestParams.enablePointInTimeRestore = true;
    backupRequestParams.schedulingFrequency = 100000L;
    assertEquals(
        ScheduleUtil.getBackupIntervalForPITRestore(backupRequestParams).toSeconds(),
        backupRequestParams.schedulingFrequency / 1000L);
  }

  @Test
  public void testGetBackupIntervalForPITRestoreSecsWithIncrementalBackups() {
    backupRequestParams.enablePointInTimeRestore = true;
    backupRequestParams.schedulingFrequency = 100000L;
    backupRequestParams.incrementalBackupFrequency = 50000L;
    assertEquals(
        ScheduleUtil.getBackupIntervalForPITRestore(backupRequestParams).toSeconds(),
        backupRequestParams.incrementalBackupFrequency / 1000L);
  }

  @Test
  public void testGetFinalHistoryRetentionUniverseForPITRestoreSecsNewMax() {
    backupRequestParams.enablePointInTimeRestore = true;
    backupRequestParams.incrementalBackupFrequency = 100000L;
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        backupRequestParams,
        TaskType.CreateBackup,
        500000L,
        null,
        true /* useLocalTimezone */,
        TimeUnit.HOURS,
        "test-1");
    backupRequestParams.incrementalBackupFrequency = 120000L;
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        backupRequestParams,
        TaskType.CreateBackup,
        500000L,
        null,
        true /* useLocalTimezone */,
        TimeUnit.HOURS,
        "test-2");
    backupRequestParams.incrementalBackupFrequency = 150000L;
    assertEquals(
        ScheduleUtil.getFinalHistoryRetentionUniverseForPITRestore(
                defaultUniverse.getUniverseUUID(),
                backupRequestParams,
                false /* toBeDeleted */,
                Duration.ofSeconds(0))
            .toSeconds(),
        150L);
  }

  @Test
  public void testGetFinalHistoryRetentionUniverseForPITRestoreSecsNoChange() {
    backupRequestParams.enablePointInTimeRestore = true;
    backupRequestParams.incrementalBackupFrequency = 100000L;
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        backupRequestParams,
        TaskType.CreateBackup,
        500000L,
        null,
        true /* useLocalTimezone */,
        TimeUnit.HOURS,
        "test-1");
    backupRequestParams.incrementalBackupFrequency = 120000L;
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        backupRequestParams,
        TaskType.CreateBackup,
        500000L,
        null,
        true /* useLocalTimezone */,
        TimeUnit.HOURS,
        "test-2");
    backupRequestParams.incrementalBackupFrequency = 110000L;
    assertEquals(
        ScheduleUtil.getFinalHistoryRetentionUniverseForPITRestore(
                defaultUniverse.getUniverseUUID(),
                backupRequestParams,
                false /* toBeDeleted */,
                Duration.ofSeconds(0))
            .toSeconds(),
        0L);
  }

  @Test
  public void testGetFinalHistoryRetentionUniverseForPITRestoreSecsDelete() {
    backupRequestParams.enablePointInTimeRestore = true;
    backupRequestParams.incrementalBackupFrequency = 100000L;
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        backupRequestParams,
        TaskType.CreateBackup,
        500000L,
        null,
        true /* useLocalTimezone */,
        TimeUnit.HOURS,
        "test-1");
    backupRequestParams.incrementalBackupFrequency = 120000L;
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        backupRequestParams,
        TaskType.CreateBackup,
        500000L,
        null,
        true /* useLocalTimezone */,
        TimeUnit.HOURS,
        "test-2");
    backupRequestParams.incrementalBackupFrequency = 150000L;
    assertEquals(
        ScheduleUtil.getFinalHistoryRetentionUniverseForPITRestore(
                defaultUniverse.getUniverseUUID(),
                backupRequestParams,
                true /* toBeDeleted */,
                Duration.ofSeconds(0))
            .toSeconds(),
        120L);
  }

  @Test
  public void testGetFinalHistoryRetentionUniverseForPITRestoreSecsAllDeleted() {
    backupRequestParams.enablePointInTimeRestore = true;
    backupRequestParams.incrementalBackupFrequency = 110000L;
    assertEquals(
        ScheduleUtil.getFinalHistoryRetentionUniverseForPITRestore(
                defaultUniverse.getUniverseUUID(),
                backupRequestParams,
                true /* toBeDeleted */,
                Duration.ofSeconds(100))
            .toSeconds(),
        -1L);
  }
}
