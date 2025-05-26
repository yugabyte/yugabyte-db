// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.Schedule.State.Active;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.cronutils.model.CronType;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZonedDateTime;
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
    Schedule.updateNewBackupScheduleTimeAndStatusAndSave(
        defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Editing, params);
    Schedule updatedSchedule =
        Schedule.getOrBadRequest(defaultCustomer.getUuid(), s1.getScheduleUUID());
    assertEquals(updatedSchedule.getFrequency(), 1200000L);
    assertEquals(updatedSchedule.getStatus(), State.Editing);
    assertEquals(updatedSchedule.getFrequencyTimeUnit(), TimeUnit.MILLISECONDS);
    assertNotEquals(updatedSchedule.getNextScheduleTaskTime(), nextScheduleTimeInitial);
  }

  @Test
  public void testUpdateStatus() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    assertEquals(s1.getStatus(), State.Active);
    Schedule updatedSchedule =
        Schedule.updateStatusAndSave(
            defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Editing);
    assertEquals(updatedSchedule.getStatus(), State.Editing);
  }

  @Test
  public void testUpdateStatusFail() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    UUID scheduleUUID = s1.getScheduleUUID();
    assertEquals(s1.getStatus(), State.Active);
    Schedule updatedSchedule =
        Schedule.updateStatusAndSave(
            defaultCustomer.getUuid(), s1.getScheduleUUID(), State.Editing);
    assertEquals(updatedSchedule.getStatus(), State.Editing);
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

  @Test
  public void testCreateScheduleBackupCronUTC() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            UUID.randomUUID(),
            new BackupRequestParams(),
            TaskType.CreateBackup,
            0L,
            "0 0 * * *",
            false /* useLocalTimezone */,
            null,
            null);
    CronParser unixCronParser =
        new CronParser(CronDefinitionBuilder.instanceDefinitionFor(CronType.UNIX));
    ExecutionTime executionTime = ExecutionTime.forCron(unixCronParser.parse("0 0 * * *"));
    Date lastScheduledTime = new Date();
    Instant instant = lastScheduledTime.toInstant();
    ZonedDateTime zonedDateTime = instant.atZone(ZoneId.of("UTC"));
    Duration duration = executionTime.timeToNextExecution(zonedDateTime).get();
    Date nextScheduleTime = new Date(lastScheduledTime.getTime() + duration.toMillis());
    assertTrue(
        Math.abs(nextScheduleTime.getTime() - schedule.getNextScheduleTaskTime().getTime()) < 10);
  }

  @Test
  public void testUpdateBackupScheduleCron() {
    BackupRequestParams params = new BackupRequestParams();
    params.incrementalBackupFrequency = 900000L;
    params.incrementalBackupFrequencyTimeUnit = TimeUnit.MILLISECONDS;
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            UUID.randomUUID(),
            params,
            TaskType.CreateBackup,
            0L,
            "0 0 * * *",
            false /* useLocalTimezone */,
            null,
            null);

    long initialNextExpectedScheduleTaskTime =
        schedule.getNextIncrementScheduleTaskTime().getTime();
    // Test only hits condition when nextIncrement is empty.
    schedule.updateNextIncrementScheduleTaskTime(null);
    params = Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    params.cronExpression = "0 1 * * *";
    schedule =
        Schedule.updateNewBackupScheduleTimeAndStatusAndSave(
            defaultCustomer.getUuid(), schedule.getScheduleUUID(), State.Editing, params);
    assertNotEquals(
        initialNextExpectedScheduleTaskTime, schedule.getNextIncrementScheduleTaskTime().getTime());
    assertTrue(
        Math.abs(
                schedule.getNextScheduleTaskTime().getTime()
                    + params.incrementalBackupFrequency
                    - schedule.getNextIncrementScheduleTaskTime().getTime())
            < 10);
  }

  @Test
  public void testNextExpectedTaskTimeFrequencyBasicUpdate() {
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(),
            UUID.randomUUID(),
            s3StorageConfig.getConfigUUID(),
            TaskType.CreateBackup);
    // Get the current time - 500 milliseconds
    Date currentTime = new Date(System.currentTimeMillis() - 500);
    Date nextSchedule = schedule.nextExpectedTaskTime(currentTime);
    assertEquals(currentTime.getTime() + schedule.getFrequency(), nextSchedule.getTime());
  }

  @Test
  public void testNextExpectedTaskTimeFrequencyMissedUpdate() {
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(),
            UUID.randomUUID(),
            s3StorageConfig.getConfigUUID(),
            TaskType.CreateBackup);
    // Get the current time - 1500 milliseconds - it should still update to a future time. with a
    // Frequency of 1000, the next time should take into account the missed interval and schedule
    // one
    // 500 milliseconds from now.
    Date currentTime = new Date(System.currentTimeMillis() - 1500);
    Date nextSchedule = schedule.nextExpectedTaskTime(currentTime);
    assertEquals(currentTime.getTime() + (2 * schedule.getFrequency()), nextSchedule.getTime());
  }

  @Test
  public void testToggleOffOn() {
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(),
            UUID.randomUUID(),
            s3StorageConfig.getConfigUUID(),
            TaskType.CreateBackup);
    schedule.updateBacklogStatus(false);
    assertEquals(State.Active, schedule.getStatus());
    Schedule.toggleBackupSchedule(
        defaultCustomer.getUuid(), schedule.getScheduleUUID(), State.Stopped, true);
    schedule.refresh();
    assertEquals(State.Stopped, schedule.getStatus());
    assertNotEquals(true, schedule.isBacklogStatus());
    schedule.updateNextScheduleTaskTime(
        new Date(System.currentTimeMillis() + 10000)); // Some time in the future
    Schedule.toggleBackupSchedule(
        defaultCustomer.getUuid(), schedule.getScheduleUUID(), State.Active, true);
    schedule.refresh();
    assertEquals(State.Active, schedule.getStatus());
    // Also validate that backlog is not set for non-expired schedules
    // isBacklogStatus should either be false or null;
    assertNotEquals(true, schedule.isBacklogStatus());
  }

  @Test
  public void testToggleOffOnExpiredBackup() {
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(),
            UUID.randomUUID(),
            s3StorageConfig.getConfigUUID(),
            TaskType.CreateBackup);
    schedule.updateBacklogStatus(false);
    schedule.setStatus(State.Stopped);
    schedule.updateNextScheduleTaskTime(
        new Date(System.currentTimeMillis() - 1000)); // Some time in the past
    schedule.updateNextIncrementScheduleTaskTime(
        new Date(System.currentTimeMillis() - 1000)); // Some time in the past
    Schedule.toggleBackupSchedule(
        defaultCustomer.getUuid(), schedule.getScheduleUUID(), State.Active, true);
    schedule.refresh();
    assertEquals(State.Active, schedule.getStatus());
    assertTrue(schedule.isBacklogStatus());
    assertTrue(schedule.isIncrementBacklogStatus());
  }
}
