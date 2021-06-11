// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import java.util.ArrayList;
import java.util.List;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import com.yugabyte.yw.commissioner.tasks.subtasks.CreateAlertDefinitions;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;

import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AlertRouteTest extends FakeDBApplication {

  private Customer defaultCustomer;

  private Universe universe;

  private AlertReceiver receiver;

  private AlertDefinition definition;

  private AlertDefinitionService alertDefinitionService = new AlertDefinitionService();

  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());

    receiver =
        AlertReceiver.create(
            defaultCustomer.getUuid(),
            AlertUtils.createParamsInstance(AlertReceiver.TargetType.Slack));
    definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);

    when(runtimeConfigFactory.forCustomer(defaultCustomer)).thenReturn(getApp().config());
  }

  @Test
  public void testCreateAndListByDefinition() {
    AlertingData data = new AlertingData();
    data.sendAlertsToYb = false;
    data.alertingEmail = "";
    data.reportOnlyErrors = true;
    data.enableClockSkew = false;
    // Setup alerting data.
    CustomerConfig.createAlertConfig(defaultCustomer.uuid, Json.toJson(data));

    CreateAlertDefinitions task =
        new CreateAlertDefinitions(alertDefinitionService, runtimeConfigFactory);
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    task.initialize(taskParams);
    task.run();

    List<AlertDefinition> definitions =
        AlertDefinition.list(
            new AlertDefinitionFilter().setCustomerUuid(defaultCustomer.getUuid()).setActive(true));
    assertNotEquals(0, definitions.size());

    for (AlertDefinition definition : definitions) {
      AlertRoute.create(defaultCustomer.getUuid(), definition.getUuid(), receiver.getUuid());
    }

    for (AlertDefinition definition : definitions) {
      assertEquals(1, AlertRoute.listByDefinition(definition.getUuid()).size());
    }
  }

  @Test
  public void testGet() {
    AlertRoute route =
        AlertRoute.create(defaultCustomer.getUuid(), definition.getUuid(), receiver.getUuid());

    AlertRoute fromDb = AlertRoute.get(route.getUuid());
    assertNotNull(fromDb);
    assertEquals(route, fromDb);
  }

  @Test
  public void testListByReceiver() {
    AlertRoute route =
        AlertRoute.create(defaultCustomer.getUuid(), definition.getUuid(), receiver.getUuid());
    AlertRoute route2 = createWithDefinition(receiver);

    List<AlertRoute> routes = AlertRoute.listByReceiver(receiver.getUuid());
    assertEquals(2, routes.size());
    assertTrue(routes.get(0).equals(route) || routes.get(0).equals(route2));
    assertTrue(routes.get(1).equals(route) || routes.get(1).equals(route2));
  }

  private AlertRoute createWithDefinition(AlertReceiver receiver) {
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    return AlertRoute.create(defaultCustomer.getUuid(), definition.getUuid(), receiver.getUuid());
  }

  @Test
  public void testListByCustomer() {
    List<AlertRoute> routes = new ArrayList<>();
    for (int i = 0; i < 10; i++) {
      routes.add(createWithDefinition(receiver));
    }

    List<AlertRoute> routes2 = AlertRoute.listByCustomer(defaultCustomer.uuid);
    assertEquals(routes.size(), routes2.size());
    for (AlertRoute route : routes) {
      assertTrue(routes2.contains(route));
    }
  }
}
