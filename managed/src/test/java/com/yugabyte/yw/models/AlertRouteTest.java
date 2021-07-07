// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

import static org.junit.Assert.*;

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
        AlertRoute.create(
            defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));

    AlertRoute fromDb = AlertRoute.get(defaultCustomer.getUuid(), route.getUuid());
    assertNotNull(fromDb);
    assertEquals(route, fromDb);
  }

  @Test
  public void testListByCustomer() {
    List<AlertRoute> routes = new ArrayList<>();
    for (int i = 0; i < 10; i++) {
      routes.add(
          AlertRoute.create(
              defaultCustomer.getUuid(),
              ALERT_ROUTE_NAME + " " + i,
              Collections.singletonList(receiver)));
    }

    List<AlertRoute> routes2 = AlertRoute.listByCustomer(defaultCustomer.uuid);
    assertEquals(routes.size(), routes2.size());
    for (AlertRoute route : routes) {
      assertTrue(routes2.contains(route));
    }
  }

  @Test
  public void testCreateWithUnsavedReceiverFails() {
    AlertReceiver receiver = new AlertReceiver();
    receiver.setUuid(UUID.randomUUID());
    try {
      AlertRoute.create(
          defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }

  @Test
  public void testCreateWithoutReceiversFails() {
    try {
      AlertRoute.create(defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.emptyList());
      fail("Missed expected exception.");
    } catch (Exception e) {
    }

    try {
      AlertRoute.create(defaultCustomer.getUuid(), ALERT_ROUTE_NAME, null);
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }

  @Test
  public void testCreateNonDefaultRoute() {
    AlertRoute.create(
        defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
    assertNull(AlertRoute.getDefaultRoute(defaultCustomer.uuid));
  }

  @Test
  public void testCreateDefaultRoute() {
    AlertRoute firstDefault =
        AlertRoute.create(
            defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver), true);
    assertEquals(firstDefault, AlertRoute.getDefaultRoute(defaultCustomer.uuid));
  }

  @Test
  public void testNameUniquenessCheck() {
    Customer secondCustomer = ModelFactory.testCustomer();
    AlertRoute.create(
        defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
    AlertRoute.create(
        secondCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
    try {
      AlertRoute.create(
          defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver));
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }
}
