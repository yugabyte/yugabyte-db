// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.Schedule.State.Active;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class ScheduleTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private CustomerConfig s3StorageConfig;
  private Users defaultUser;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST27");
    defaultUser = ModelFactory.testUser(defaultCustomer);
    // Set http context
    TestUtils.setFakeHttpContext(defaultUser);
  }

  @Test
  public void testCreateBackup() {
    UUID universeUUID = UUID.randomUUID();
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    assertNotNull(schedule);
    BackupTableParams taskParams = Json.fromJson(schedule.getTaskParams(), BackupTableParams.class);
    assertEquals(s3StorageConfig.getConfigUUID(), taskParams.storageConfigUUID);
    assertEquals(Active, schedule.getStatus());
  }

  @Test
  public void testFetchByScheduleUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    Schedule schedule = Schedule.getOrBadRequest(s.getScheduleUUID());
    assertNotNull(schedule);
  }

  @Test
  public void testGetAllActiveSchedulesWithAllActive() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Schedule s2 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    List<Schedule> schedules = Schedule.getAllActive();
    assertEquals(2, schedules.size());
  }

  @Test
  public void testGetAllActiveSchedulesWithInactive() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Schedule s2 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    List<Schedule> schedules = Schedule.getAllActive();
    assertEquals(2, schedules.size());
    s2.stopSchedule();
    schedules = Schedule.getAllActive();
    assertEquals(1, schedules.size());
  }

  @Test
  public void testScheduleLockAlreadyRunningFails() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Schedule.modifyScheduleRunningAndSave(
        defaultCustomer.getUuid(), s1.getScheduleUUID(), true /* isRunning */);
    RuntimeException re =
        assertThrows(
            RuntimeException.class,
            () ->
                Schedule.modifyScheduleRunningAndSave(
                    defaultCustomer.getUuid(), s1.getScheduleUUID(), true /* isRunning */));
    assertTrue(re.getMessage().contains("Schedule is currently locked"));
  }

  @Test
  public void testScheduleOnlyLockActiveFailsWithStopped() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Schedule.updateStatusAndSave(defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Stopped);

    RuntimeException re =
        assertThrows(
            RuntimeException.class,
            () ->
                Schedule.modifyScheduleRunningAndSave(
                    defaultCustomer.getUuid(),
                    s1.getScheduleUUID(),
                    true /* isRunning */,
                    true /* onlyLockIfActive */));
    assertTrue(re.getMessage().contains("Schedule is not active"));
  }

  @Test
  public void testUpdateNewBackupScheduleTimeAndStatusAndSave() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Date nextScheduleTimeInitial = s1.getNextScheduleTaskTime();
    BackupRequestParams params = Json.fromJson(s1.getTaskParams(), BackupRequestParams.class);
    params.schedulingFrequency = 1200000L;
    params.frequencyTimeUnit = TimeUnit.MILLISECONDS;
    s1 =
        Schedule.updateNewBackupScheduleTimeAndStatusAndSave(
            defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Editing, params);
    assertEquals(s1.getFrequency(), 1200000L);
    assertEquals(s1.getStatus(), State.Editing);
    assertEquals(s1.getFrequencyTimeUnit(), TimeUnit.MILLISECONDS);
    assertNotEquals(s1.getNextScheduleTaskTime(), nextScheduleTimeInitial);
  }

  @Test
  public void testUpdateStatus() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    assertEquals(s1.getStatus(), State.Active);
    s1 =
        Schedule.updateStatusAndSave(
            defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Editing);
    assertEquals(s1.getStatus(), State.Editing);
  }

  @Test
  public void testUpdateStatusFail() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    UUID scheduleUUID = s1.getScheduleUUID();
    assertEquals(s1.getStatus(), State.Active);
    s1 =
        Schedule.updateStatusAndSave(
            defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Editing);
    assertEquals(s1.getStatus(), State.Editing);
    RuntimeException ex =
        assertThrows(
            RuntimeException.class,
            () ->
                Schedule.updateStatusAndSave(
                    defaultCustomer.getUuid(),
                    scheduleUUID,
                    State.Creating /* invalid transition */));
    assertTrue(
        ex.getMessage().contains("Transition of Schedule from Editing to Creating not allowed"));
  }
}
