// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Date;
import java.util.UUID;
import org.apache.commons.lang.time.DateUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class SchedulerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(SchedulerTest.class);

  private static Commissioner mockCommissioner;
  private CustomerConfig s3StorageConfig;
  com.yugabyte.yw.scheduler.Scheduler scheduler;
  Customer defaultCustomer;
  PlatformScheduler mockPlatformScheduler;

  @Before
  public void setUp() {
    mockPlatformScheduler = mock(PlatformScheduler.class);
    mockCommissioner = mock(Commissioner.class);
    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST28");

    scheduler = new Scheduler(mockPlatformScheduler, mockCommissioner, mockTaskManager);
  }

  @Test
  public void schedulerDeletesExpiredBackups() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    // Test that we do not delete backups of paused universe
    setUniversePaused(true, universe);
    scheduler.scheduleRunner();
    assertEquals(0, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(null, CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID));
    verify(mockCommissioner, times(0)).submit(any(), any());

    // Unpause the universe and make sure that we will delete the backup.
    setUniversePaused(false, universe);
    scheduler.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredChildIncrementalBackup() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    Backup backup2 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    backup2.transitionState(Backup.BackupState.Completed);
    backup2.baseBackupUUID = UUID.randomUUID();
    backup2.save();

    scheduler.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());

    backup2.baseBackupUUID = backup2.backupUUID;
    backup2.save();
    assertEquals(2, Backup.getExpiredBackups().get(defaultCustomer).size());
    scheduler.scheduleRunner();
    verify(mockCommissioner, times(3)).submit(any(), any());
  }

  @Test
  public void schedulerDeletesExpiredBackups_universeDeleted() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);
    universe.delete();
    scheduler.scheduleRunner();

    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
  }

  @Test
  public void schedulerDeletesExpiredBackupsCreatedFromSchedule() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    UUID fakeScheduleUUID = s.getScheduleUUID();
    for (int i = 0; i < 5; i++) {
      Backup backup =
          ModelFactory.createExpiredBackupWithScheduleUUID(
              defaultCustomer.uuid,
              universe.universeUUID,
              s3StorageConfig.configUUID,
              fakeScheduleUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    for (int i = 0; i < 2; i++) {
      Backup backup =
          ModelFactory.createBackupWithExpiry(
              defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    scheduler.scheduleRunner();
    assertEquals(7, Backup.getExpiredBackups().get(defaultCustomer).size());

    // 4 times for deleting expired backups and 1 time for creating backup, total 5 calls.
    verify(mockCommissioner, times(5)).submit(any(), any());
  }

  @Test
  public void testSkippedFutureScheduleTask() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    s.updateNextScheduleTaskTime(DateUtils.addHours(new Date(), 2));
    scheduler.scheduleRunner();
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testClearScheduleBacklog() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    s.updateBacklogStatus(true);
    scheduler.scheduleRunner();
    verify(mockCommissioner, times(1)).submit(any(), any());
    s.refresh();
    assertEquals(false, s.getBacklogStatus());
  }

  @Test
  public void testEnableScheduleBacklog() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    setUniverseBackupInProgress(true, universe);
    scheduler.scheduleRunner();
    verify(mockCommissioner, times(0)).submit(any(), any());
    s.refresh();
    assertEquals(true, s.getBacklogStatus());
  }

  @Test
  public void testSkipScheduleTaskIfRunning() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    ScheduleTask.create(UUID.randomUUID(), s.getScheduleUUID());
    scheduler.scheduleRunner();
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testSkipAlreadyRunningDeleteBackupTask() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockTaskManager.isDeleteBackupTaskAlreadyPresent(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(true);
    scheduler.scheduleRunner();
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(null, CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID));
    verify(mockCommissioner, times(0)).submit(any(), any());

    when(mockTaskManager.isDeleteBackupTaskAlreadyPresent(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(false);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    scheduler.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  public static void setUniverseBackupInProgress(boolean value, Universe universe) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.backupInProgress = value;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universe.universeUUID, updater);
  }

  public static void setUniversePaused(boolean value, Universe universe) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.universePaused = value;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universe.universeUUID, updater);
  }
}
