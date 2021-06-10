// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import java.util.Set;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;

import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateAlertDefinitionsTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(CreateAlertDefinitionsTest.class);

  private Customer customer;

  private Universe u;

  private int activeDefinitions = 0;

  @Before
  public void setUp() {
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
    Customer customer = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(customer.getCustomerId());
    CreateAlertDefinitionsExt alertDefinitionTask = new CreateAlertDefinitionsExt();
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.setParams(taskParams);

    assertEquals(0, AlertDefinition.listActive(customer.uuid).size());
    createAlertData(true);

    alertDefinitionTask.run();

    LOG.info("Trying to get list of alerts.");
    Set<AlertDefinition> createdDefinitions = AlertDefinition.listActive(customer.uuid);
    assertEquals(activeDefinitions, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertFalse(definition.query.contains("__nodePrefix__"));
      assertFalse(definition.query.contains("__value__"));
      assertTrue(definition.isActive);
    }
  }

  @Test
  public void testRunFunctionality_ClockSkewTemplateDisabled() {
    CreateAlertDefinitionsExt alertDefinitionTask = new CreateAlertDefinitionsExt();
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.setParams(taskParams);

    createAlertData(false);

    assertEquals(0, AlertDefinition.listActive(customer.uuid).size());

    alertDefinitionTask.run();

    Set<AlertDefinition> createdDefinitions = AlertDefinition.listActive(customer.uuid);
    assertEquals(activeDefinitions - 1, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertNotEquals(AlertDefinitionTemplate.CLOCK_SKEW.getName(), definition.name);
      assertTrue(definition.isActive);
    }
  }

  @Test
  public void testRunFunctionality_NoAlertConfigExist() {
    CreateAlertDefinitionsExt alertDefinitionTask = new CreateAlertDefinitionsExt();
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.setParams(taskParams);

    assertEquals(0, AlertDefinition.listActive(customer.uuid).size());

    alertDefinitionTask.run();

    Set<AlertDefinition> createdDefinitions = AlertDefinition.listActive(customer.uuid);
    assertEquals(activeDefinitions, createdDefinitions.size());
  }

  private static class CreateAlertDefinitionsExt extends CreateAlertDefinitions {

    public void setParams(ITaskParams taskParams) {
      this.taskParams = taskParams;
    }
  }
}
