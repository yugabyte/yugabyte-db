// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;

import play.api.Play;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerTaskManagerTest extends FakeDBApplication {
  Customer customer;
  Universe universe;
  CustomerTaskManager taskManager;

  private CustomerTask createTask(
      CustomerTask.TargetType targetType, UUID targetUUID, CustomerTask.TaskType taskType) {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse);
    UUID taskUUID = UUID.randomUUID();
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.setTaskDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.save();
    return CustomerTask.create(
        customer, targetUUID, taskInfo.getTaskUUID(), targetType, taskType, "Foo");
  }

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
  }

  @Test
  public void testFailPendingTasksNoneExist() throws Exception {
    universe = ModelFactory.createUniverse(customer.getCustomerId());
    taskManager = spy(Play.current().injector().instanceOf(CustomerTaskManager.class));
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe)) targetUUID = universe.universeUUID;
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.get(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Success);
      taskInfo.save();
      th.markAsCompleted();
    }

    taskManager.failAllPendingTasks();
    // failPendingTask should never be called since all tasks are already completed
    verify(taskManager, times(0)).failPendingTask(any(), any());
  }

  @Test
  public void testFailPendingTasksForCompletedCustomerTask() throws Exception {
    universe = ModelFactory.createUniverse(customer.getCustomerId());
    taskManager = spy(Play.current().injector().instanceOf(CustomerTaskManager.class));
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe)) targetUUID = universe.universeUUID;
      CustomerTask th = createTask(targetType, targetUUID, Create);
      // CustomerTask is marked completed, but TaskInfo is still in Create state.
      th.markAsCompleted();
    }

    taskManager.failAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .failPendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.uuid).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals("Platform restarted.", taskInfo.getTaskDetails().get("errorString").asText());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
  }

  @Test
  public void testFailPendingTasksForRunningTaskInfo() throws Exception {
    universe = ModelFactory.createUniverse(customer.getCustomerId());
    taskManager = spy(Play.current().injector().instanceOf(CustomerTaskManager.class));
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe)) targetUUID = universe.universeUUID;
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.get(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Running);
      // CustomerTask is NOT marked completed, but TaskInfo is Running state.
      taskInfo.save();
    }

    taskManager.failAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .failPendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.uuid).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      assertNotNull(task.getCompletionTime());
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals("Platform restarted.", taskInfo.getTaskDetails().get("errorString").asText());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
  }

  @Test
  public void testFailPendingTasksForCompletedTaskInfo() throws Exception {
    universe = ModelFactory.createUniverse(customer.getCustomerId());
    taskManager = spy(Play.current().injector().instanceOf(CustomerTaskManager.class));
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe)) targetUUID = universe.universeUUID;
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.get(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Success);
      // CustomerTask is NOT marked completed, but TaskInfo is Running state.
      taskInfo.save();
    }

    taskManager.failAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .failPendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.uuid).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      assertNotNull(task.getCompletionTime());
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    }
  }
}
