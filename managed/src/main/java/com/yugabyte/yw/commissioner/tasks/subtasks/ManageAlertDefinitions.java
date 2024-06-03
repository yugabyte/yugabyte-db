// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageAlertDefinitions extends UniverseTaskBase {

  private final AlertDefinitionService alertDefinitionService;

  @Inject
  public ManageAlertDefinitions(
      BaseTaskDependencies baseTaskDependencies, AlertDefinitionService alertDefinitionService) {
    super(baseTaskDependencies);
    this.alertDefinitionService = alertDefinitionService;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static class Params extends UniverseTaskParams {
    // Defines if alert definitions should be activated or deactivated
    public boolean active;
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

      AlertDefinitionFilter filter =
          AlertDefinitionFilter.builder()
              .customerUuid(customer.getUuid())
              .label(KnownAlertLabels.UNIVERSE_UUID, universe.getUniverseUUID().toString())
              .build();

      List<AlertDefinition> definitions =
          alertDefinitionService.list(filter).stream()
              .map(definition -> definition.setActive(taskParams().active))
              .map(definition -> definition.setConfigWritten(false))
              .collect(Collectors.toList());

      // Just need to save - AlertConfigurationWriter will pick up
      // and create or remove definition files.
      alertDefinitionService.save(definitions);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
