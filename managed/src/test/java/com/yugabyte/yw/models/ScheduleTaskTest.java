// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.common.FakeDBApplication;
import java.util.Date;
import java.util.UUID;
import org.junit.Test;

public class ScheduleTaskTest extends FakeDBApplication {

  public ScheduleTask createScheduleTask(UUID taskUUID) {
    UUID scheduleUUID = UUID.randomUUID();
    return ScheduleTask.create(taskUUID, scheduleUUID);
  }

  @Test
  public void testCreateTask() {
    ScheduleTask task = createScheduleTask(UUID.randomUUID());
    assertNotNull(task);
    assertNotNull(task.getScheduledTime());
  }

  @Test
  public void testFetchByTaskUUID() {
    UUID taskUUID = UUID.randomUUID();
    ScheduleTask task = createScheduleTask(taskUUID);
    ScheduleTask t = ScheduleTask.fetchByTaskUUID(taskUUID);
    assertNotNull(t);
  }

  @Test
  public void testGetCompletedTimeNull() {
    ScheduleTask task = createScheduleTask(UUID.randomUUID());
    Date time = task.getCompletedTime();
    assertNull(time);
  }

  @Test
  public void testCompletedTime() {
    ScheduleTask task = createScheduleTask(UUID.randomUUID());
    task.markCompleted();
    Date time = task.getCompletedTime();
    assertNotNull(time);
  }
}
