// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.Alert.TargetType;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

import java.util.List;
import java.util.UUID;

@RunWith(JUnitParamsRunner.class)
public class AlertTest extends FakeDBApplication {

  private static final String TEST_ALERT_CODE = "TEST_ALERT_1";

  private static final String TEST_ALERT_CODE_2 = "TEST_ALERT_2";

  private Customer cust1;

  private Customer cust2;

  @Before
  public void setUp() {
    cust1 = ModelFactory.testCustomer("Customer 1");
    cust2 = ModelFactory.testCustomer("Customer 2");
  }

  @Test
  public void testAdd() {
    Alert alert = Alert.create(cust1.uuid, TEST_ALERT_CODE, "Warning", "Testing alert.");
      assertNotNull(alert.uuid);
      assertEquals(cust1.uuid, alert.customerUUID);
      assertEquals("Warning", alert.type);
      assertEquals("Testing alert.", alert.message);
  }

  @Test
  public void testAlertsSameCustomer() {
    Alert alert1 = Alert.create(cust1.uuid, TEST_ALERT_CODE, "Warning", "Testing alert 1.");
    Alert alert2 = Alert.create(cust1.uuid, TEST_ALERT_CODE_2, "Warning", "Testing alert 2.");
    List<Alert> cust1Alerts = Alert.list(cust1.uuid);
    List<Alert> cust2Alerts = Alert.list(cust2.uuid);

    assertThat(cust1Alerts, hasItem(alert1));
    assertThat(cust1Alerts, hasItem(alert2));

    assertThat(cust2Alerts, not(hasItem(alert1)));
    assertThat(cust2Alerts, not(hasItem(alert2)));
  }

  @Test
  public void testAlertsDiffCustomer() {
    Alert alert1 = Alert.create(cust1.uuid, TEST_ALERT_CODE, "Warning", "Testing alert 1.");
    Alert alert2 = Alert.create(cust2.uuid, TEST_ALERT_CODE_2, "Warning", "Testing alert 2.");
    List<Alert> cust1Alerts = Alert.list(cust1.uuid);
    List<Alert> cust2Alerts = Alert.list(cust2.uuid);

    assertThat(cust1Alerts, hasItem(alert1));
    assertThat(cust1Alerts, not(hasItem(alert2)));

    assertThat(cust2Alerts, not(hasItem(alert1)));
    assertThat(cust2Alerts, hasItem(alert2));
  }

  @Test
  @Parameters(method = "parametersToTestAlertsTypes")
  public void testAlertsTypes(Alert.TargetType alertType, UUID uuid, Class<?> claz) {
    Alert alert = Alert.create(cust1.uuid, uuid, alertType,
        TEST_ALERT_CODE, "Warning", "Testing alert.");
    assertNotNull(alert.uuid);
    assertEquals(cust1.uuid, alert.customerUUID);
    assertEquals(uuid, alert.targetUUID);
    assertEquals("Warning", alert.type);
    assertEquals("Testing alert.", alert.message);
  }

  @SuppressWarnings("unused")
  private Object[] parametersToTestAlertsTypes() {
    // @formatter:off
    return new Object[] {
        new Object[] { Alert.TargetType.TaskType, UUID.randomUUID(), AbstractTaskBase.class },
        new Object[] { Alert.TargetType.UniverseType, UUID.randomUUID(), Universe.class },
    };
    // @formatter:on
  }

  @Test
  public void testExists_ErrCode_Exists() {
    assertFalse(Alert.exists(TEST_ALERT_CODE));
    Alert.create(cust1.uuid, TEST_ALERT_CODE, "Warning", "Testing alert 1.");
    assertTrue(Alert.exists(TEST_ALERT_CODE));
  }

  @Test
  public void testExists_ErrCode_DoesntExists() {
    assertFalse(Alert.exists(TEST_ALERT_CODE));
    Alert.create(cust1.uuid, TEST_ALERT_CODE_2, "Warning", "Testing alert 2.");
    assertFalse(Alert.exists(TEST_ALERT_CODE));
  }

  @Test
  public void testList_CustomerUUID_ErrCode_TargetUUID() {
    UUID targetUUID = UUID.randomUUID();
    assertEquals(0, Alert.list(cust1.uuid, TEST_ALERT_CODE, targetUUID).size());

    Alert alert1 = Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE,
        "Warning", "Testing alert 1.");
    // One sec pause to have alert2.createTime > alert1.createTime.
    try {
      Thread.sleep(1000);
    } catch (InterruptedException e) {
    }
    Alert alert2 = Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE,
        "Warning", "Testing alert 1.");

    Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE_2, "Warning",
        "Testing alert 2.");
    Alert.create(cust2.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE, "Warning",
        "Testing alert 3.");
    Alert.create(cust2.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE_2, "Warning",
        "Testing alert 4.");
    Alert.create(cust1.uuid, UUID.randomUUID(), TargetType.UniverseType, TEST_ALERT_CODE, "Warning",
        "Testing alert 5.");

    List<Alert> list = Alert.list(cust1.uuid, TEST_ALERT_CODE, targetUUID);
    assertEquals(2, list.size());
    assertEquals(alert2, list.get(0));
    assertEquals(alert1, list.get(1));
  }

  @Test
  public void testGetActiveCustomerAlerts() {
    UUID targetUUID = UUID.randomUUID();
    Universe universe = ModelFactory.createUniverse(cust1.getCustomerId());
    AlertDefinition definition = AlertDefinition.create(cust1.uuid, universe.universeUUID,
        "alertDefinition", "query {{ test.parameter }}", true);

    Alert alert1 = Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE,
        "Warning", "Testing alert 1.");
    alert1.definitionUUID = definition.uuid;
    alert1.save();

    Alert alert2 = Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE,
        "Warning", "Testing alert 2.");
    alert2.state = State.ACTIVE;
    alert2.definitionUUID = definition.uuid;
    alert2.save();

    assertEquals(2, Alert.getActiveCustomerAlerts(cust1.uuid, definition.uuid).size());
  }

  @Test
  public void testGetActiveCustomerAlertsByTargetUuid() {
    UUID targetUUID = UUID.randomUUID();
    Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE,
        "Warning", "Testing alert 1.");

    Alert alert2 = Alert.create(cust1.uuid, targetUUID, TargetType.UniverseType, TEST_ALERT_CODE,
        "Warning", "Testing alert 2.");
    alert2.state = State.ACTIVE;
    alert2.save();

    Alert alert3 = Alert.create(cust1.uuid, UUID.randomUUID(), TargetType.UniverseType,
        TEST_ALERT_CODE, "Warning", "Testing alert 3.");
    alert3.state = State.ACTIVE;
    alert3.save();

    List<Alert> result = Alert.getActiveCustomerAlertsByTargetUuid(cust1.uuid, targetUUID);

    assertEquals(2, result.size());
    assertFalse(result.contains(alert3));
  }
}
