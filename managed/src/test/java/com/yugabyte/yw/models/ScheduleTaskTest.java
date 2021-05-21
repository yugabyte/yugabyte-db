// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;

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
    task.setCompletedTime();
    Date time = task.getCompletedTime();
    assertNotNull(time);
  }
}
