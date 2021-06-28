// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertUtils;
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
    receiver =
        AlertReceiver.create(
            defaultCustomer.getUuid(),
            "Test AlertReceiver",
            AlertUtils.createParamsInstance(AlertReceiver.TargetType.Slack));
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
              defaultCustomer.getUuid(), ALERT_ROUTE_NAME, Collections.singletonList(receiver)));
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
}
