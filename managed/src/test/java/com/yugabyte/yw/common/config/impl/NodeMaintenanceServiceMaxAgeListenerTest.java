// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.config.impl;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Test;

public class NodeMaintenanceServiceMaxAgeListenerTest extends FakeDBApplication {

  @Test
  public void testUniverseScopeRewritesOnlyTheNotRunAlert() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = ModelFactory.createUniverse(customer.getId());
    AlertDefinition notRun =
        writtenDefinition(customer, universe, AlertTemplate.NODE_MAINTENANCE_SERVICE_NOT_RUN);
    AlertDefinition failed =
        writtenDefinition(customer, universe, AlertTemplate.NODE_MAINTENANCE_SERVICE_FAILED);

    new NodeMaintenanceServiceMaxAgeListener(alertConfigurationService, alertDefinitionService)
        .processUniverse(universe);

    assertFalse(alertDefinitionService.get(notRun.getUuid()).isConfigWritten());
    assertTrue(alertDefinitionService.get(failed.getUuid()).isConfigWritten());
  }

  private AlertDefinition writtenDefinition(
      Customer customer, Universe universe, AlertTemplate template) {
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(customer, universe, c -> c.setTemplate(template));
    AlertDefinition definition =
        ModelFactory.createAlertDefinition(customer, universe, configuration);
    definition.setConfigWritten(true);
    definition.save();
    return definition;
  }
}
