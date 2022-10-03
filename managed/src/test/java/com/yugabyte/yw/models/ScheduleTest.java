// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.Schedule.State.Active;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class ScheduleTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private CustomerConfig s3StorageConfig;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST27");
  }

  @Test
  public void testCreateBackup() {
    UUID universeUUID = UUID.randomUUID();
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, universeUUID, s3StorageConfig.configUUID);
    assertNotNull(schedule);
    BackupTableParams taskParams = Json.fromJson(schedule.getTaskParams(), BackupTableParams.class);
    assertEquals(s3StorageConfig.configUUID, taskParams.storageConfigUUID);
    assertEquals(Active, schedule.getStatus());
  }

  @Test
  public void testFetchByScheduleUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, u.universeUUID, s3StorageConfig.configUUID);
    Schedule schedule = Schedule.getOrBadRequest(s.scheduleUUID);
    assertNotNull(schedule);
  }

  @Test
  public void testGetAllActiveSchedulesWithAllActive() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    Schedule s2 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    List<Schedule> schedules = Schedule.getAllActive();
    assertEquals(2, schedules.size());
  }

  @Test
  public void testGetAllActiveSchedulesWithInactive() {
    Schedule s1 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    Schedule s2 =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    List<Schedule> schedules = Schedule.getAllActive();
    assertEquals(2, schedules.size());
    s2.stopSchedule();
    schedules = Schedule.getAllActive();
    assertEquals(1, schedules.size());
  }
}
