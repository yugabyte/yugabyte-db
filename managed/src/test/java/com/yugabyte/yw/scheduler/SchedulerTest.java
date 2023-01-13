// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
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

    scheduler = new Scheduler(mockPlatformScheduler, mockCommissioner);
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
  public void testRetryTaskOnServiceUnavailable() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universe.universeUUID, s3StorageConfig.configUUID);
    Date dt = new Date();
    s.updateNextScheduleTaskTime(dt);
    s.setCronExpression("0 0 * * *");
    s.save();
    when(mockCommissioner.submit(any(), any()))
        .thenThrow(new PlatformServiceException(SERVICE_UNAVAILABLE, "you shall not pass"));
    scheduler.scheduleRunner();
    s = Schedule.getOrBadRequest(s.scheduleUUID);
    Date next = s.getNextScheduleTaskTime();
    assertTrue(next.before(DateUtils.addHours(new Date(), 1)));
  }

  public static void setUniverseBackupInProgress(boolean value, Universe universe) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.updateInProgress = value;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universe.universeUUID, updater);
  }
}
