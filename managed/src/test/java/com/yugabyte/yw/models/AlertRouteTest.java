// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertRouteTest extends FakeDBApplication {

  private static final String ALERT_ROUTE_NAME = "Test AlertRoute";

  private Customer defaultCustomer;

  private AlertReceiver receiver;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    receiver = ModelFactory.createEmailReceiver(defaultCustomer, "Test AlertReceiver");
  }

  @Test
  public void testGet() {
    AlertRoute route =
        ModelFactory.createAlertRoute(
            defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));

    AlertRoute fromDb = AlertRoute.get(defaultCustomer.getUuid(), route.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(route));
  }

  @Test
  public void testNameUniquenessCheck() {
    Customer secondCustomer = ModelFactory.testCustomer();
    ModelFactory.createAlertRoute(
        defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
    ModelFactory.createAlertRoute(
        secondCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
    try {
      ModelFactory.createAlertRoute(
          defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }
}
