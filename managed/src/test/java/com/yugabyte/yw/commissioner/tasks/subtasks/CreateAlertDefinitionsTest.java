// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroupTarget;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateAlertDefinitionsTest extends FakeDBApplication {

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private AlertService alertService = new AlertService();
  private AlertDefinitionService alertDefinitionService = new AlertDefinitionService(alertService);
  private AlertDefinitionGroupService alertDefinitionGroupService =
      new AlertDefinitionGroupService(alertDefinitionService, runtimeConfigFactory);

  private Customer customer;

  private Universe u;

  private int plannedDefinitions = 0;

  @Before
  public void setUp() {
    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);
    when(baseTaskDependencies.getAlertDefinitionService()).thenReturn(alertDefinitionService);
    when(baseTaskDependencies.getAlertDefinitionGroupService())
        .thenReturn(alertDefinitionGroupService);

    customer = ModelFactory.testCustomer();
    u = ModelFactory.createUniverse(customer.getCustomerId());

    for (AlertDefinitionTemplate template : AlertDefinitionTemplate.values()) {
      ModelFactory.createAlertDefinitionGroup(
          customer,
          u,
          g ->
              g.setTarget(
                  new AlertDefinitionGroupTarget().setAll(template.isCreateForNewCustomer())));
      if (template.isCreateForNewCustomer()) {
        plannedDefinitions++;
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
  public void testRunFunctionality() {
    createAlertData(true);

    CreateAlertDefinitions alertDefinitionTask = new CreateAlertDefinitions(baseTaskDependencies);
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = u.universeUUID;
    alertDefinitionTask.initialize(taskParams);

    AlertDefinitionFilter definitionFilter =
        AlertDefinitionFilter.builder().customerUuid(customer.uuid).build();
    assertEquals(0, alertDefinitionService.list(definitionFilter).size());

    alertDefinitionTask.run();

    List<AlertDefinition> createdDefinitions = alertDefinitionService.list(definitionFilter);
    assertEquals(plannedDefinitions, createdDefinitions.size());
    for (AlertDefinition definition : createdDefinitions) {
      assertFalse(definition.getQuery().contains("__nodePrefix__"));
      assertFalse(definition.getQuery().contains("__value__"));
    }
  }
}
