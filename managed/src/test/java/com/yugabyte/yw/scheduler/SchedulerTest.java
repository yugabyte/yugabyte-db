// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Matchers;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import scala.concurrent.ExecutionContext;

@RunWith(MockitoJUnitRunner.class)
public class SchedulerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(SchedulerTest.class);

  private static Commissioner mockCommissioner;
  private CustomerConfig s3StorageConfig;
  @Mock private Scheduler mockScheduler;
  com.yugabyte.yw.scheduler.Scheduler scheduler;
  Customer defaultCustomer;
  ActorSystem mockActorSystem;
  ExecutionContext mockExecutionContext;

  @Before
  public void setUp() {
    mockActorSystem = mock(ActorSystem.class);
    mockExecutionContext = mock(ExecutionContext.class);
    mockCommissioner = mock(Commissioner.class);
    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST28");

    when(mockActorSystem.scheduler()).thenReturn(mockScheduler);
    scheduler =
        new com.yugabyte.yw.scheduler.Scheduler(
            mockActorSystem, mockExecutionContext, mockCommissioner);
  }

  @Test
  public void schedulerDeletesExpiredBackups() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(), Matchers.any())).thenReturn(fakeTaskUUID);

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
  public void schedulerDeletesExpiredBackups_universeDeleted() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(), Matchers.any())).thenReturn(fakeTaskUUID);

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
    when(mockCommissioner.submit(Matchers.any(), Matchers.any())).thenReturn(fakeTaskUUID);
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

  public static void setUniversePaused(boolean value, Universe universe) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.universePaused = value;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universe.universeUUID, updater);
  }
}
