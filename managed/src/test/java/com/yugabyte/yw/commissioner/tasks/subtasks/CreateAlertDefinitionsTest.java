// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;

import play.libs.Json;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.List;

import static org.junit.Assert.*;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class CreateAlertDefinitionsTest extends FakeDBApplication {

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private AlertDefinitionService alertDefinitionService = new AlertDefinitionService();

  private Customer customer;

  private Universe u;

  private int activeDefinitions = 0;

  @Before
  public void setUp() {
    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);
    when(baseTaskDependencies.getAlertDefinitionService()).thenReturn(alertDefinitionService);

    customer = ModelFactory.testCustomer();
    u = ModelFactory.createUniverse(customer.getCustomerId());

    activeDefinitions = 0;
    for (AlertDefinitionTemplate template : AlertDefinitionTemplate.values()) {
      if (template.isCreateForNewUniverse()) {
        activeDefinitions++;
      }
    }
  }

  private void createAlertData(boolean enableClockSkew) {
    AlertingData data = new AlertingData();
    data.sendAlertsToYb = false;
    data.alertingEmail = "";
    data.reportOnlyErrors = true;
    data.enableClockSkew = enableClockSkew;
    // Setup alerting data.
    CustomerConfig.createAlertConfig(customer.uuid, Json.toJson(data));
  }

  @Test
  public void testRunFunctionality_NoDisabledTemplates() {

    when(runtimeConfigFactory.forCustomer(customer)).thenReturn(getApp().config());
    createAlertData(true);

    CreateAlertDefinitions alertDefinitionTask = new CreateAlertDefinitions(baseTaskDependencies);
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.initialize(taskParams);

    AlertDefinitionFilter activeDefinitionsFilter =
        AlertDefinitionFilter.builder().customerUuid(customer.uuid).active(true).build();
    assertEquals(0, alertDefinitionService.list(activeDefinitionsFilter).size());

    alertDefinitionTask.run();

    List<AlertDefinition> createdDefinitions = alertDefinitionService.list(activeDefinitionsFilter);
    assertEquals(activeDefinitions, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertFalse(definition.getQuery().contains("__nodePrefix__"));
      assertFalse(definition.getQuery().contains("__value__"));
      assertTrue(definition.isActive());
    }
  }

  @Test
  public void testRunFunctionality_ClockSkewTemplateDisabled() {
    CreateAlertDefinitions alertDefinitionTask = new CreateAlertDefinitions(baseTaskDependencies);
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.initialize(taskParams);

    when(runtimeConfigFactory.forCustomer(customer)).thenReturn(getApp().config());
    createAlertData(false);

    AlertDefinitionFilter activeDefinitionsFilter =
        AlertDefinitionFilter.builder().customerUuid(customer.uuid).active(true).build();
    assertEquals(0, alertDefinitionService.list(activeDefinitionsFilter).size());

    alertDefinitionTask.run();

    List<AlertDefinition> createdDefinitions = alertDefinitionService.list(activeDefinitionsFilter);
    assertEquals(activeDefinitions - 1, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertNotEquals(AlertDefinitionTemplate.CLOCK_SKEW.getName(), definition.getName());
      assertTrue(definition.isActive());
    }
  }

  @Test
  public void testRunFunctionality_NoAlertConfigExist() {
    CreateAlertDefinitions alertDefinitionTask = new CreateAlertDefinitions(baseTaskDependencies);
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.initialize(taskParams);

    when(runtimeConfigFactory.forCustomer(customer)).thenReturn(getApp().config());

    AlertDefinitionFilter activeDefinitionsFilter =
        AlertDefinitionFilter.builder().customerUuid(customer.uuid).active(true).build();
    assertEquals(0, alertDefinitionService.list(activeDefinitionsFilter).size());

    alertDefinitionTask.run();

    List<AlertDefinition> createdDefinitions = alertDefinitionService.list(activeDefinitionsFilter);
    assertEquals(activeDefinitions, createdDefinitions.size());
  }
}
