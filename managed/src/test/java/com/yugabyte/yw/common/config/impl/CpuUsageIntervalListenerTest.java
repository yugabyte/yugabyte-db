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

public class CpuUsageIntervalListenerTest extends FakeDBApplication {

  @Test
  public void testUniverseScopeRewritesBothCpuAlerts() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = ModelFactory.createUniverse(customer.getId());
    AlertDefinition vmCpu = writtenDefinition(customer, universe, AlertTemplate.NODE_CPU_USAGE);
    AlertDefinition k8sCpu = writtenDefinition(customer, universe, AlertTemplate.DB_K8S_CPU_USAGE);
    AlertDefinition memory =
        writtenDefinition(customer, universe, AlertTemplate.MEMORY_CONSUMPTION);

    new CpuUsageIntervalListener(alertConfigurationService, alertDefinitionService)
        .processUniverse(universe);

    assertFalse(alertDefinitionService.get(vmCpu.getUuid()).isConfigWritten());
    assertFalse(alertDefinitionService.get(k8sCpu.getUuid()).isConfigWritten());
    assertTrue(alertDefinitionService.get(memory.getUuid()).isConfigWritten());
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
