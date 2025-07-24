// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerTaskManagerTest extends FakeDBApplication {
  Customer customer;
  Universe universe;
  CustomerTaskManager taskManager;
  YBClient mockClient;

  private CustomerTask createTask(
      CustomerTask.TargetType targetType, UUID targetUUID, CustomerTask.TaskType taskType) {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse, null);
    UUID taskUUID = UUID.randomUUID();
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.save();
    return CustomerTask.create(
        customer, targetUUID, taskInfo.getUuid(), targetType, taskType, "Foo");
  }

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    taskManager = app.injector().instanceOf(CustomerTaskManager.class);
    when(mockCommissioner.isTaskRetryable(any(), any())).thenReturn(true);
  }

  @Test
  @Ignore
  public void testFailPendingTasksNoneExist() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Success);
      taskInfo.save();
      th.markAsCompleted();
    }

    taskManager.handleAllPendingTasks();
    // failPendingTask should never be called since all tasks are already completed
    verify(taskManager, times(0)).handlePendingTask(any(), any());
  }

  @Test
  @Ignore
  public void testHandlePendingTasksForCompletedCustomerTask() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    mockClient = mock(YBClient.class);
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      // CustomerTask is marked completed, but TaskInfo is still in Create state.
      th.markAsCompleted();
    }

    taskManager.handleAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .handlePendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.getUuid()).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals("Platform restarted.", taskInfo.getTaskError().getMessage());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
  }

  @Test
  @Ignore
  public void testFailPendingTasksForRunningTaskInfo() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    mockClient = mock(YBClient.class);
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Running);
      // CustomerTask is NOT marked completed, but TaskInfo is Running state.
      taskInfo.save();
    }

    taskManager.handleAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .handlePendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.getUuid()).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      assertNotNull(task.getCompletionTime());
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals("Platform restarted.", taskInfo.getTaskError().getMessage());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
  }

  @Test
  @Ignore
  public void testFailPendingTasksForCompletedTaskInfo() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    mockClient = mock(YBClient.class);
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Success);
      // CustomerTask is NOT marked completed, but TaskInfo is Running state.
      taskInfo.save();
    }

    taskManager.handleAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .handlePendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.getUuid()).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      assertNotNull(task.getCompletionTime());
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    }
  }

  @Test
  public void testAutoRetryAbortedTasks() {
    List<TaskInfo> retryableTasks = new ArrayList<>();
    TaskType.filteredValues().stream()
        .filter(t -> !t.getCustomerTaskIds().isEmpty())
        .filter(t -> Commissioner.isTaskTypeRetryable(t))
        .forEach(
            t -> {
              TaskInfo taskInfo = new TaskInfo(t, null);
              taskInfo.setTaskParams(Json.newObject());
              taskInfo.setOwner("");
              taskInfo.setYbaVersion(Util.getYbaVersion());
              taskInfo.setTaskState(TaskInfo.State.Aborted);
              taskInfo.setTaskError(new YBAError(Code.PLATFORM_SHUTDOWN, "Platform shutdown"));
              taskInfo.save();
              Pair<CustomerTask.TaskType, CustomerTask.TargetType> pair =
                  Iterables.getFirst(t.getCustomerTaskIds(), null);
              CustomerTask cTask =
                  CustomerTask.create(
                      customer,
                      UUID.randomUUID(),
                      taskInfo.getUuid(),
                      pair.getSecond(),
                      pair.getFirst(),
                      "FakeTarget");
              cTask.setCompletionTime(new Date());
              cTask.save();
              retryableTasks.add(taskInfo);
            });
    Set<UUID> nonAutoRetryableTaskUuids = new HashSet<>();
    List<List<TaskInfo>> partitions =
        Lists.partition(retryableTasks, (int) Math.ceil(retryableTasks.size() / 4.0));
    // Set old version for the first partition.
    partitions.get(0).stream()
        .forEach(
            t -> {
              t.setYbaVersion("1.1.0.0-b1");
              t.save();
              nonAutoRetryableTaskUuids.add(t.getUuid());
            });
    // Set very old task with the same version.
    partitions.get(1).stream()
        .forEach(
            t -> {
              t.setCreateTime(Date.from(Instant.now().minus(1, ChronoUnit.DAYS)));
              t.save();
              nonAutoRetryableTaskUuids.add(t.getUuid());
            });
    // Set a different failure reason.
    partitions.get(2).stream()
        .forEach(
            t -> {
              t.setTaskError(new YBAError(Code.UNKNOWN_ERROR, "Unknown error"));
              t.save();
              nonAutoRetryableTaskUuids.add(t.getUuid());
            });
    Set<UUID> autoRetryableTaskUuids =
        partitions.get(3).stream().map(TaskInfo::getUuid).collect(Collectors.toSet());
    taskManager.autoRetryAbortedTasks(
        Duration.ofMinutes(10),
        ct -> {
          TaskInfo taskInfo = TaskInfo.getOrBadRequest(ct.getTaskUUID());
          assertFalse(
              String.format("Non retryable task %s(%s)", ct.getTaskUUID(), taskInfo),
              nonAutoRetryableTaskUuids.contains(ct.getTaskUUID()));
          assertTrue(
              String.format("Already retried task %s(%s)", ct.getTaskUUID(), taskInfo),
              autoRetryableTaskUuids.remove(ct.getTaskUUID()));
        });
    assertEquals(0, autoRetryableTaskUuids.size());
  }
}
