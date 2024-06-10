// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class SetUniverseKeyTest extends FakeDBApplication {
  Customer customer1;
  Customer customer2;

  SetUniverseKey task;

  @Before
  public void setUp() {
    customer1 = ModelFactory.testCustomer();
    customer2 = ModelFactory.testCustomer("tc2", "test2@customer.com");
    task = app.injector().instanceOf(SetUniverseKey.class);
  }

  @Test
  public void testCustomerError() {
    Exception customerTaskException = new RuntimeException();
    Mockito.doThrow(customerTaskException).when(task).setCustomerUniverseKeys(any());
    Mockito.doCallRealMethod().when(task).scheduleRunner();
    task.scheduleRunner();
    // Ensure that the task runs for each customer even though they both error out
    verify(task, times(2)).setCustomerUniverseKeys(any());
    verify(task, times(1)).handleCustomerError(eq(customer1.getUuid()), eq(customerTaskException));
    verify(task, times(1)).handleCustomerError(eq(customer2.getUuid()), eq(customerTaskException));
    verify(task, times(0)).setUniverseKey(any());
  }
}
