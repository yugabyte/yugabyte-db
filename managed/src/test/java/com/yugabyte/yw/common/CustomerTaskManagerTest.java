// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
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
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.setDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.save();
    return CustomerTask.create(
        customer, targetUUID, taskInfo.getTaskUUID(), targetType, taskType, "Foo");
  }

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    taskManager = app.injector().instanceOf(CustomerTaskManager.class);
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
      assertEquals("Platform restarted.", taskInfo.getDetails().get("errorString").asText());
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
      assertEquals("Platform restarted.", taskInfo.getDetails().get("errorString").asText());
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
}
