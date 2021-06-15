// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Inject;

public class CreateAlertDefinitions extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateAlertDefinitions.class);

  private final AlertDefinitionService alertDefinitionService;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public CreateAlertDefinitions(
      AlertDefinitionService alertDefinitionService, RuntimeConfigFactory runtimeConfigFactory) {
    this.alertDefinitionService = alertDefinitionService;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  protected UniverseTaskParams taskParams() {
    return (UniverseTaskParams) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      Customer customer = Customer.get(universe.customerId);
      String nodePrefix = universe.getUniverseDetails().nodePrefix;

      Config customerConfig = runtimeConfigFactory.forCustomer(customer);

      for (AlertDefinitionTemplate definition : AlertDefinitionTemplate.values()) {
        if (definition.isCreateForNewUniverse()) {
          AlertDefinition alertDefinition = new AlertDefinition();
          alertDefinition.setCustomerUUID(customer.getUuid());
          alertDefinition.setTargetType(AlertDefinition.TargetType.Universe);
          alertDefinition.setName(definition.getName());
          alertDefinition.setQuery(definition.buildTemplate(nodePrefix));
          alertDefinition.setQueryThreshold(
              customerConfig.getDouble(definition.getDefaultThresholdParamName()));
          alertDefinition.setLabels(
              AlertDefinitionLabelsBuilder.create().appendUniverse(universe).get());
          alertDefinitionService.create(alertDefinition);
        }
      }

    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
