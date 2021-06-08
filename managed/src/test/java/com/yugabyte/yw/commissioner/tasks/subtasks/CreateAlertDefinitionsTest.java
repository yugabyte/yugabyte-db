// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.List;

import static org.junit.Assert.*;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class CreateAlertDefinitionsTest extends FakeDBApplication {

  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private AlertDefinitionService alertDefinitionService = new AlertDefinitionService();

  @Test
  public void testRunFunctionality() {
    Customer customer = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(customer.getCustomerId());
    CreateAlertDefinitionsExt alertDefinitionTask = new CreateAlertDefinitionsExt();
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.setParams(taskParams);

    when(runtimeConfigFactory.forCustomer(customer)).thenReturn(getApp().config());

    AlertDefinitionFilter activeDefinitionsFilter =
        new AlertDefinitionFilter().setCustomerUuid(customer.uuid).setActive(true);
    assertEquals(0, alertDefinitionService.list(activeDefinitionsFilter).size());
    int adCount = 0;
    for (AlertDefinitionTemplate template : AlertDefinitionTemplate.values()) {
      if (template.isCreateForNewUniverse()) {
        adCount++;
      }
    }

    alertDefinitionTask.run();

    List<AlertDefinition> createdDefinitions = alertDefinitionService.list(activeDefinitionsFilter);
    assertEquals(adCount, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertFalse(definition.getQuery().contains("__nodePrefix__"));
      assertFalse(definition.getQuery().contains("__value__"));
      assertTrue(definition.isActive());
    }
  }

  private class CreateAlertDefinitionsExt extends CreateAlertDefinitions {

    private CreateAlertDefinitionsExt() {
      super(alertDefinitionService, runtimeConfigFactory);
    }

    public void setParams(ITaskParams taskParams) {
      this.taskParams = taskParams;
    }
  }
}
