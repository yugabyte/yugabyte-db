// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.models;

import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;

import java.util.Date;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class CustomerTaskTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe defaultUniverse;

  @Before
  public void setUp() {
    defaultCustomer = Customer.create("Test", "test@test.com", "foo");
    defaultUniverse = Universe.create("Test Universe", defaultCustomer.customerId);
  }

  @Test
  public void testCreateInstance() throws InterruptedException {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");
    assertEquals(th.getTarget(), CustomerTask.TargetType.Universe);
    Thread.sleep(5000);
    Date currentDate  = new Date();
    assertTrue(currentDate.after(th.getCreateTime()));
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Universe : Foo"))));
    assertThat(th.getUniverseUUID(), is(equalTo(defaultUniverse.universeUUID)));
    assertThat(th.getCustomerUUID(), is(equalTo(defaultCustomer.uuid)));
  }

  @Test
  public void testMarkTaskComplete() throws InterruptedException {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");
    assertEquals(th.getTarget(), CustomerTask.TargetType.Universe);
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Universe : Foo"))));
    Thread.sleep(1000);
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Created Universe : Foo"))));
    assertTrue(th.getCreateTime().before(th.getCompletionTime()));
  }

  @Test
  public void testFriendlyDescriptions() {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask th;

    th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Universe : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Created Universe : Foo"))));

    th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Update, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updating Universe : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updated Universe : Foo"))));

    th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Delete, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleting Universe : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleted Universe : Foo"))));

    th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Table, CustomerTask.TaskType.Create, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Creating Table : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Created Table : Foo"))));

    th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Table, CustomerTask.TaskType.Update, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updating Table : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Updated Table : Foo"))));

    th = CustomerTask.create(defaultCustomer, defaultUniverse, taskUUID, CustomerTask.TargetType.Table, CustomerTask.TaskType.Delete, "Foo");
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleting Table : Foo"))));
    th.markAsCompleted();
    assertThat(th.getFriendlyDescription(), is(allOf(notNullValue(), equalTo("Deleted Table : Foo"))));
  }
}
