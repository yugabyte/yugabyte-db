// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;

import play.libs.Json;

public class CreateAlertDefinitions extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateAlertDefinitions.class);

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
      Universe universe = Universe.get(taskParams().universeUUID);
      Customer customer = Customer.get(universe.customerId);
      String nodePrefix = universe.getUniverseDetails().nodePrefix;

      CustomerConfig alertConfig = CustomerConfig.getAlertConfig(customer.uuid);
      AlertingData data =
          alertConfig == null ? null : Json.fromJson(alertConfig.getData(), AlertingData.class);

      for (AlertDefinitionTemplate template : AlertDefinitionTemplate.values()) {
        if (template.isCreateForNewUniverse()) {
          // w/a for 2.4.x - __value__ = 0) as it should not be used in templates for new universes.
          AlertDefinition.create(
              customer.uuid,
              universe.universeUUID,
              template.getName(),
              template.buildTemplate(nodePrefix, 0),
              template != AlertDefinitionTemplate.CLOCK_SKEW
                  || (data == null)
                  || data.enableClockSkew);
        }
      }

    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
