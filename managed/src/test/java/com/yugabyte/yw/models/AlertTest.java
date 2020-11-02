// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

import java.util.*;
import java.util.concurrent.*;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static org.mockito.Matchers.anyList;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

public class AlertTest extends FakeDBApplication {
  private Customer cust1;
  private Customer cust2;

  @Before
  public void setUp() {
    cust1 = ModelFactory.testCustomer("Customer 1");
    cust2 = ModelFactory.testCustomer("Customer 2");
  }

  @Test
  public void testAdd() {
      Alert alert = Alert.create(cust1.uuid, "TEST_ALERT_1", "Warning", "Testing alert.");
      assertNotNull(alert.uuid);
      assertEquals(cust1.uuid, alert.customerUUID);
      assertEquals("Warning", alert.type);
      assertEquals("Testing alert.", alert.message);
  }

  @Test
  public void testAlertsSameCustomer() {
    Alert alert1 = Alert.create(cust1.uuid, "TEST_ALERT_1", "Warning", "Testing alert 1.");
    Alert alert2 = Alert.create(cust1.uuid, "TEST_ALERT_2", "Warning", "Testing alert 2.");
    List<Alert> cust1Alerts = Alert.list(cust1.uuid);
    List<Alert> cust2Alerts = Alert.list(cust2.uuid);

    assertThat(cust1Alerts, hasItem(alert1));
    assertThat(cust1Alerts, hasItem(alert2));

    assertThat(cust2Alerts, not(hasItem(alert1)));
    assertThat(cust2Alerts, not(hasItem(alert2)));
  }

  @Test
  public void testAlertsDiffCustomer() {
    Alert alert1 = Alert.create(cust1.uuid, "TEST_ALERT_1", "Warning", "Testing alert 1.");
    Alert alert2 = Alert.create(cust2.uuid, "TEST_ALERT_2", "Warning", "Testing alert 2.");
    List<Alert> cust1Alerts = Alert.list(cust1.uuid);
    List<Alert> cust2Alerts = Alert.list(cust2.uuid);

    assertThat(cust1Alerts, hasItem(alert1));
    assertThat(cust1Alerts, not(hasItem(alert2)));

    assertThat(cust2Alerts, not(hasItem(alert1)));
    assertThat(cust2Alerts, hasItem(alert2));
  }

  @Test
  public void testAlertsUniverseType() {
    Universe u = ModelFactory.createUniverse();
    Alert alert = Alert.create(cust1.uuid, u.universeUUID, Alert.TargetType.UniverseType,
                               "TEST_ALERT_1", "Warning", "Testing alert.");
    assertNotNull(alert.uuid);
    assertEquals(cust1.uuid, alert.customerUUID);
    assertEquals(u.universeUUID, alert.targetUUID);
    assertEquals(Universe.class, alert.targetType.getType());
    assertEquals("Warning", alert.type);
    assertEquals("Testing alert.", alert.message);
}
}
