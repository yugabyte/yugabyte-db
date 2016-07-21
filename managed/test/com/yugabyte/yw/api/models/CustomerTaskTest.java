// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.models;

import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.api.models.Customer;
import com.yugabyte.yw.api.models.CustomerTask;
import com.yugabyte.yw.common.FakeDBApplication;

import java.util.Date;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class CustomerTaskTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = Customer.create("Test", "test@test.com", "foo");
  }

  @Test
  public void testCreateInstance() throws InterruptedException {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");
    assertEquals(th.getTarget(), CustomerTask.TargetType.Instance);
    Thread.sleep(5000);
    Date currentDate  = new Date();
    assertTrue(currentDate.after(th.getCreateTime()));
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Instance : Foo"))));
  }

  @Test
  public void testMarkTaskComplete() throws InterruptedException {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");
    assertEquals(th.getTarget(), CustomerTask.TargetType.Instance);
    Date currentDate  = new Date();
    assertTrue(currentDate.after(th.getCreateTime()));
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Instance : Foo"))));
    Thread.sleep(5000);
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Created Instance : Foo"))));
    assertTrue(th.getCreateTime().before(th.getCompletionTime()));
  }

  @Test
  public void testFriendlyDescriptions() {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask th;

    th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Instance : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Created Instance : Foo"))));

    th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Update, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updating Instance : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updated Instance : Foo"))));

    th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Delete, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleting Instance : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleted Instance : Foo"))));

    th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Table, CustomerTask.TaskType.Create, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Table : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Created Table : Foo"))));

    th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Table, CustomerTask.TaskType.Update, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updating Table : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updated Table : Foo"))));

    th = CustomerTask.create(defaultCustomer, taskUUID, CustomerTask.TargetType.Table, CustomerTask.TaskType.Delete, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleting Table : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleted Table : Foo"))));
  }
}
