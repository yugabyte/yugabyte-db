/*
 * Copyright 2025 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UnregisterUniverseFromPaCollector extends UniverseTaskBase {

  private final PerfAdvisorService perfAdvisorService;

  @Inject
  protected UnregisterUniverseFromPaCollector(
      BaseTaskDependencies baseTaskDependencies, PerfAdvisorService perfAdvisorService) {
    super(baseTaskDependencies);
    this.perfAdvisorService = perfAdvisorService;
  }

  public static class Params extends UniverseTaskParams {}

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Customer customer = Customer.get(universe.getCustomerId());
      UUID paCollectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
      if (paCollectorUuid == null) {
        log.debug(
            "Universe {} is not registered with PA Collector, skipping",
            universe.getUniverseUUID());
        return;
      }

      PACollector collector =
          perfAdvisorService.getOrBadRequest(customer.getUuid(), paCollectorUuid);
      perfAdvisorService.deleteUniverse(collector, universe);
      Universe.saveDetails(
          taskParams().getUniverseUUID(), u -> u.getUniverseDetails().setPaCollectorUuid(null));
      log.info(
          "Unregistered universe {} from PA Collector {}",
          universe.getUniverseUUID(),
          paCollectorUuid);
    } catch (Exception e) {
      log.warn(
          "Failed to unregister universe {} from PA Collector: {}. Clearing paCollectorUuid.",
          taskParams().getUniverseUUID(),
          e.getMessage());
      try {
        Universe.saveDetails(
            taskParams().getUniverseUUID(), u -> u.getUniverseDetails().setPaCollectorUuid(null));
      } catch (Exception e2) {
        log.warn("Failed to clear paCollectorUuid from universe details: {}", e2.getMessage());
        throw new RuntimeException(getName() + " failed: " + e.getMessage(), e);
      }
    }
  }
}
