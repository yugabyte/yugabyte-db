// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ManageAlertDefinitionsTest extends FakeDBApplication {

  private Customer customer;

  private Universe u;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    u = ModelFactory.createUniverse(customer.getId());
  }

  @Test
  public void testRunFunctionality() {
    AlertConfiguration configuration = ModelFactory.createAlertConfiguration(customer, u);
    AlertDefinition universeDefinition =
        ModelFactory.createAlertDefinition(customer, u, configuration);
    Universe otherUniverse = ModelFactory.createUniverse("Other", customer.getId());
    AlertDefinition otherUniverseDefinition =
        ModelFactory.createAlertDefinition(customer, otherUniverse, configuration);
    AlertDefinition customerDefinition =
        ModelFactory.createAlertDefinition(customer, null, configuration);

    assertTrue(universeDefinition.isActive());
    assertTrue(otherUniverseDefinition.isActive());
    assertTrue(customerDefinition.isActive());

    ManageAlertDefinitions.Params params = new ManageAlertDefinitions.Params();
    params.setUniverseUUID(u.getUniverseUUID());
    params.active = false;

    ManageAlertDefinitions manageAlertDefinitions =
        app.injector().instanceOf(ManageAlertDefinitions.class);
    manageAlertDefinitions.initialize(params);

    manageAlertDefinitions.run();

    universeDefinition = alertDefinitionService.get(universeDefinition.getUuid());
    otherUniverseDefinition = alertDefinitionService.get(otherUniverseDefinition.getUuid());
    customerDefinition = alertDefinitionService.get(customerDefinition.getUuid());
    assertFalse(universeDefinition.isActive());
    assertTrue(otherUniverseDefinition.isActive());
    assertTrue(customerDefinition.isActive());

    manageAlertDefinitions = app.injector().instanceOf(ManageAlertDefinitions.class);
    params = new ManageAlertDefinitions.Params();
    params.setUniverseUUID(u.getUniverseUUID());
    params.active = true;
    manageAlertDefinitions.initialize(params);

    manageAlertDefinitions.run();

    universeDefinition = alertDefinitionService.get(universeDefinition.getUuid());
    otherUniverseDefinition = alertDefinitionService.get(otherUniverseDefinition.getUuid());
    customerDefinition = alertDefinitionService.get(customerDefinition.getUuid());
    assertTrue(universeDefinition.isActive());
    assertTrue(otherUniverseDefinition.isActive());
    assertTrue(customerDefinition.isActive());
  }
}
