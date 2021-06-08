// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.Alert.TargetType;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.hasSize;
import static org.junit.Assert.*;

@RunWith(JUnitParamsRunner.class)
public class AlertTest extends FakeDBApplication {

  private static final String TEST_ALERT_CODE = "TEST_ALERT_1";

  private static final String TEST_ALERT_CODE_2 = "TEST_ALERT_2";

  private static final String TEST_LABEL = "test_label";
  private static final String TEST_LABEL_VALUE = "test_value";

  private static final String TEST_LABEL_2 = "test_label_2";
  private static final String TEST_LABEL_VALUE_2 = "test_value2";

  private Customer cust1;

  private Customer cust2;

  private AlertDefinitionService alertDefinitionService;

  @Before
  public void setUp() {
    cust1 = ModelFactory.testCustomer("Customer 1");
    cust2 = ModelFactory.testCustomer("Customer 2");
    alertDefinitionService = new AlertDefinitionService();
  }

  @Test
  public void testAddAndGet() {
    AlertDefinition definition = createDefinition();
    Alert alert = createTestAlert(definition);

    Alert queriedAlert = Alert.get(alert.uuid);

    assertTestAlert(queriedAlert, definition);
  }

  @Test
  public void testUpdateAndQueryByLabel() {
    AlertDefinition definition = createDefinition();
    Alert alert = createTestAlert(definition);

    alert.update("New message");

    AlertLabel oldLabel1 = new AlertLabel(alert, TEST_LABEL, TEST_LABEL_VALUE);
    AlertLabel oldLabel2 = new AlertLabel(alert, TEST_LABEL_2, TEST_LABEL_VALUE_2);
    List<Alert> queriedAlerts = Alert.get(cust1.uuid, oldLabel1);

    assertThat(queriedAlerts, hasSize(1));
    Alert queriedAlert = queriedAlerts.get(0);
    assertThat(queriedAlert.message, is("New message"));
    assertThat(queriedAlert.getLabels(), containsInAnyOrder(oldLabel1, oldLabel2));
  }

  @Test
  public void testDelete() {
    AlertDefinition definition = createDefinition();
    Alert alert = createTestAlert(definition);

    alert.delete();

    Alert queriedAlert = Alert.get(alert.uuid);

    assertThat(queriedAlert, Matchers.nullValue());
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
    Alert alert =
        Alert.create(cust1.uuid, uuid, alertType, TEST_ALERT_CODE, "Warning", "Testing alert.");
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
      new Object[] {Alert.TargetType.TaskType, UUID.randomUUID(), AbstractTaskBase.class},
      new Object[] {Alert.TargetType.UniverseType, UUID.randomUUID(), Universe.class},
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

    Alert alert1 =
        Alert.create(
            cust1.uuid,
            targetUUID,
            TargetType.UniverseType,
            TEST_ALERT_CODE,
            "Warning",
            "Testing alert 1.");
    // One sec pause to have alert2.createTime > alert1.createTime.
    try {
      Thread.sleep(1000);
    } catch (InterruptedException e) {
    }
    Alert alert2 =
        Alert.create(
            cust1.uuid,
            targetUUID,
            TargetType.UniverseType,
            TEST_ALERT_CODE,
            "Warning",
            "Testing alert 1.");

    Alert.create(
        cust1.uuid,
        targetUUID,
        TargetType.UniverseType,
        TEST_ALERT_CODE_2,
        "Warning",
        "Testing alert 2.");
    Alert.create(
        cust2.uuid,
        targetUUID,
        TargetType.UniverseType,
        TEST_ALERT_CODE,
        "Warning",
        "Testing alert 3.");
    Alert.create(
        cust2.uuid,
        targetUUID,
        TargetType.UniverseType,
        TEST_ALERT_CODE_2,
        "Warning",
        "Testing alert 4.");
    Alert.create(
        cust1.uuid,
        UUID.randomUUID(),
        TargetType.UniverseType,
        TEST_ALERT_CODE,
        "Warning",
        "Testing alert 5.");

    List<Alert> list = Alert.list(cust1.uuid, TEST_ALERT_CODE, targetUUID);
    assertEquals(2, list.size());
    assertEquals(alert2, list.get(0));
    assertEquals(alert1, list.get(1));
  }

  @Test
  public void testGetActiveCustomerAlerts() {
    UUID targetUUID = UUID.randomUUID();
    AlertDefinition definition = createDefinition();

    Alert alert1 =
        Alert.create(
            cust1.uuid,
            targetUUID,
            TargetType.UniverseType,
            TEST_ALERT_CODE,
            "Warning",
            "Testing alert 1.");
    alert1.definitionUUID = definition.getUuid();
    alert1.save();

    Alert alert2 =
        Alert.create(
            cust1.uuid,
            targetUUID,
            TargetType.UniverseType,
            TEST_ALERT_CODE,
            "Warning",
            "Testing alert 2.");
    alert2.state = State.ACTIVE;
    alert2.definitionUUID = definition.getUuid();
    alert2.save();

    assertEquals(2, Alert.getActiveCustomerAlerts(cust1.uuid, definition.getUuid()).size());
  }

  @Test
  public void testGetActiveCustomerAlertsByTargetUuid() {
    UUID targetUUID = UUID.randomUUID();
    Alert.create(
        cust1.uuid,
        targetUUID,
        TargetType.UniverseType,
        TEST_ALERT_CODE,
        "Warning",
        "Testing alert 1.");

    Alert alert2 =
        Alert.create(
            cust1.uuid,
            targetUUID,
            TargetType.UniverseType,
            TEST_ALERT_CODE,
            "Warning",
            "Testing alert 2.");
    alert2.state = State.ACTIVE;
    alert2.save();

    Alert alert3 =
        Alert.create(
            cust1.uuid,
            UUID.randomUUID(),
            TargetType.UniverseType,
            TEST_ALERT_CODE,
            "Warning",
            "Testing alert 3.");
    alert3.state = State.ACTIVE;
    alert3.save();

    List<Alert> result = Alert.getActiveCustomerAlertsByTargetUuid(cust1.uuid, targetUUID);

    assertEquals(2, result.size());
    assertFalse(result.contains(alert3));
  }

  public AlertDefinition createDefinition() {
    Universe universe = ModelFactory.createUniverse(cust1.getCustomerId());
    return ModelFactory.createAlertDefinition(cust1, universe);
  }

  public Alert createTestAlert(AlertDefinition definition) {
    AlertLabel label = new AlertLabel(TEST_LABEL, TEST_LABEL_VALUE);
    AlertLabel label2 = new AlertLabel(TEST_LABEL_2, TEST_LABEL_VALUE_2);
    return Alert.create(
        cust1.uuid,
        definition.getUniverseUUID(),
        TargetType.UniverseType,
        TEST_ALERT_CODE,
        "Warning",
        "Testing alert 1.",
        true,
        definition.getUuid(),
        ImmutableList.of(label, label2));
  }

  private void assertTestAlert(Alert alert, AlertDefinition definition) {
    AlertLabel label = new AlertLabel(alert, TEST_LABEL, TEST_LABEL_VALUE);
    AlertLabel label2 = new AlertLabel(alert, TEST_LABEL_2, TEST_LABEL_VALUE_2);
    assertThat(alert.customerUUID, is(cust1.uuid));
    assertThat(alert.targetUUID, is(definition.getUniverseUUID()));
    assertThat(alert.targetType, is(TargetType.UniverseType));
    assertThat(alert.errCode, is(TEST_ALERT_CODE));
    assertThat(alert.type, is("Warning"));
    assertThat(alert.message, is("Testing alert 1."));
    assertThat(alert.definitionUUID, equalTo(definition.getUuid()));
    assertTrue(alert.sendEmail);
    assertThat(alert.getLabels(), containsInAnyOrder(label, label2));
  }
}
