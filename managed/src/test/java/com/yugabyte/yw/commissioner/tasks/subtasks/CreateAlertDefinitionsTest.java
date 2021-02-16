// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.Set;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;

@RunWith(MockitoJUnitRunner.class)
public class CreateAlertDefinitionsTest extends FakeDBApplication {

  @Test
  public void testRunFunctionality() {
    Customer customer = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(customer.getCustomerId());
    CreateAlertDefinitionsExt alertDefinitionTask = new CreateAlertDefinitionsExt();
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.setParams(taskParams);

    assertEquals(0, AlertDefinition.listActive(customer.uuid).size());
    int adCount = 0;
    for (AlertDefinitionTemplate template : AlertDefinitionTemplate.values()) {
      if (template.isCreateForNewUniverse()) {
        adCount++;
      }
    }

    alertDefinitionTask.run();

    Set<AlertDefinition> createdDefinitions = AlertDefinition.listActive(customer.uuid);
    assertEquals(adCount, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertFalse(definition.query.contains("__nodePrefix__"));
      assertFalse(definition.query.contains("__value__"));
      assertTrue(definition.isActive);
    }
  }

  private static class CreateAlertDefinitionsExt extends CreateAlertDefinitions {

    public void setParams(ITaskParams taskParams) {
      this.taskParams = taskParams;
    }
  }
}
