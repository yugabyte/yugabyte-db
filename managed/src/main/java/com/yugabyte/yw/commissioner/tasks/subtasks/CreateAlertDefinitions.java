// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

import javax.inject.Inject;

@Slf4j
public class CreateAlertDefinitions extends UniverseTaskBase {

  @Inject
  public CreateAlertDefinitions(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
      log.info("Running {}", getName());
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      Customer customer = Customer.get(universe.customerId);
      String nodePrefix = universe.getUniverseDetails().nodePrefix;

      Config customerConfig = runtimeConfigFactory.forCustomer(customer);
      CustomerConfig alertConfig = CustomerConfig.getAlertConfig(customer.getUuid());
      AlertingData data =
          alertConfig == null ? null : Json.fromJson(alertConfig.getData(), AlertingData.class);

      for (AlertDefinitionTemplate template : AlertDefinitionTemplate.values()) {
        AlertDefinition alertDefinition =
            new AlertDefinition()
                .setActive(
                    template != AlertDefinitionTemplate.CLOCK_SKEW
                        ? template.isCreateForNewUniverse()
                        : (data == null) || data.enableClockSkew)
                .setCustomerUUID(customer.getUuid())
                .setName(template.getName())
                .setQuery(template.buildTemplate(nodePrefix))
                .setQueryThreshold(
                    customerConfig.getDouble(template.getDefaultThresholdParamName()))
                .setLabels(AlertDefinitionLabelsBuilder.create().appendTarget(universe).get());
        alertDefinitionService.create(alertDefinition);
      }

    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
