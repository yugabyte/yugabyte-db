// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertConfigurationTarget;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CreateAlertDefinitionsTest extends FakeDBApplication {

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private Customer customer;

  private Universe u;

  private int plannedDefinitions = 0;

  @Before
  public void setUp() {
    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);
    when(baseTaskDependencies.getAlertConfigurationService()).thenReturn(alertConfigurationService);
    when(baseTaskDependencies.getExecutorFactory())
        .thenReturn(app.injector().instanceOf(PlatformExecutorFactory.class));

    customer = ModelFactory.testCustomer();
    u = ModelFactory.createUniverse(customer.getId());

    AlertTemplateService alertTemplateService =
        app.injector().instanceOf(AlertTemplateService.class);
    for (AlertTemplate template : AlertTemplate.values()) {
      AlertTemplateDescription templateDescription =
          alertTemplateService.getTemplateDescription(template);
      ModelFactory.createAlertConfiguration(
          customer,
          u,
          g ->
              g.setTarget(
                  new AlertConfigurationTarget()
                      .setAll(templateDescription.isCreateForNewCustomer())));
      if (templateDescription.isCreateForNewCustomer()) {
        plannedDefinitions++;
      }
    }
  }

  @Test
  public void testRunFunctionality() {
    CreateAlertDefinitions alertDefinitionTask = new CreateAlertDefinitions(baseTaskDependencies);
    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.setUniverseUUID(u.getUniverseUUID());
    alertDefinitionTask.initialize(taskParams);

    AlertDefinitionFilter definitionFilter =
        AlertDefinitionFilter.builder().customerUuid(customer.getUuid()).build();
    assertEquals(0, alertDefinitionService.list(definitionFilter).size());

    alertDefinitionTask.run();

    List<AlertDefinition> createdDefinitions = alertDefinitionService.list(definitionFilter);
    assertEquals(plannedDefinitions, createdDefinitions.size());
  }
}
