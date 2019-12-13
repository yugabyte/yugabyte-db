// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;

import java.util.Date;
import java.util.UUID;

import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class CustomerTaskTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private CustomerTask createTask(CustomerTask.TargetType targetType,
                          UUID targetUUID, CustomerTask.TaskType taskType) {
    UUID taskUUID = UUID.randomUUID();
    return CustomerTask.create(defaultCustomer, targetUUID, taskUUID,
        targetType, taskType, "Foo");
  }

  @Test
  public void testCreateInstance() throws InterruptedException {
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      Date currentDate  = new Date();
      assertTrue(currentDate.compareTo(th.getCreateTime()) >= 0);
      assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(),
          equalTo("Creating " + targetType.toString() + " : Foo"))));
      assertThat(th.getTargetUUID(), is(equalTo(targetUUID)));
      assertThat(th.getCustomerUUID(), is(equalTo(defaultCustomer.uuid)));
    }
  }

  @Test
  public void testMarkTaskComplete() throws InterruptedException {
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      assertEquals(th.getTarget(), targetType);
      assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(),
          equalTo("Creating " + targetType.toString() + " : Foo"))));
      th.markAsCompleted();
      assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(),
          equalTo("Created " + targetType.toString() + " : Foo"))));
      assertTrue(th.getCreateTime().compareTo(th.getCompletionTime()) <= 0);
      Date completionTime = th.getCompletionTime();
      // Calling mark as completed shouldn't change the time.
      th.markAsCompleted();
      assertTrue(completionTime.equals(th.getCompletionTime()));
    }
  }

  @Test
  public void testFriendlyDescriptions() {
    UUID targetUUID = UUID.randomUUID();
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      for (CustomerTask.TaskType taskType: CustomerTask.TaskType.filteredValues()) {
        CustomerTask th = createTask(targetType, targetUUID, taskType);
        assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(),
            equalTo(taskType.toString(false) +  targetType + " : Foo"))));
        th.markAsCompleted();
        assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(),
            equalTo(taskType.toString(true) + targetType + " : Foo"))));
      }
    }
  }
}
