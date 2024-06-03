// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

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
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Customer customer = Customer.get(universe.getCustomerId());

      AlertConfigurationFilter filter =
          AlertConfigurationFilter.builder()
              .customerUuid(customer.getUuid())
              .targetType(AlertConfiguration.TargetType.UNIVERSE)
              .build();

      List<AlertConfiguration> configurations =
          alertConfigurationService.list(filter).stream()
              .filter(group -> group.getTarget().isAll())
              .collect(Collectors.toList());

      // Just need to save - service will create definition itself.
      alertConfigurationService.save(customer.getUuid(), configurations);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
