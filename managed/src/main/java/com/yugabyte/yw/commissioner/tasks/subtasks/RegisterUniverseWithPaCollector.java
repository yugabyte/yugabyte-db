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
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.pa.PaRegistrationMode;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class RegisterUniverseWithPaCollector extends UniverseTaskBase {

  private final PerfAdvisorService perfAdvisorService;

  @Inject
  protected RegisterUniverseWithPaCollector(
      BaseTaskDependencies baseTaskDependencies, PerfAdvisorService perfAdvisorService) {
    super(baseTaskDependencies);
    this.perfAdvisorService = perfAdvisorService;
  }

  public static class Params extends UniverseTaskParams {
    // When set, use explicit collector/mode values instead of auto-registration config.
    public UUID paCollectorUuid;
    public PaRegistrationMode mode;

    /** The destination, for ONLINE only. Already pushed to the collector by PushPaExportConfig. */
    public UUID paEndpointUuid;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Customer customer = Customer.get(universe.getCustomerId());

      PACollector collector;
      PaRegistrationMode mode;
      UUID paEndpointUuid = taskParams().paEndpointUuid;

      if (taskParams().paCollectorUuid != null) {
        collector =
            perfAdvisorService.getOrBadRequest(customer.getUuid(), taskParams().paCollectorUuid);
        mode = taskParams().mode != null ? taskParams().mode : PaRegistrationMode.BASIC;
      } else {
        if (!confGetter.getConfForScope(customer, CustomerConfKeys.paAutoRegistrationEnabled)) {
          log.info("PA auto-registration is disabled, skipping");
          return;
        }

        List<PACollector> collectors =
            perfAdvisorService.list(
                PACollectorFilter.builder().customerUuid(customer.getUuid()).build());
        if (CollectionUtils.isEmpty(collectors)) {
          log.info(
              "No PA Collector configured for customer {}, skipping auto-registration",
              customer.getUuid());
          return;
        }

        collector = collectors.get(0);
        // Auto-registration deliberately stays on the local modes: a misconfigured destination
        // should not be able to divert a whole fleet's data off-box on universe create.
        mode =
            PaRegistrationMode.of(
                confGetter.getConfForScope(
                    customer, CustomerConfKeys.paAutoRegistrationAdvancedObservability));
        paEndpointUuid = null;
      }

      log.info(
          "Registering universe {} with PA Collector {} (mode={}, paEndpointUuid={})",
          universe.getUniverseUUID(),
          collector.getUuid(),
          mode,
          paEndpointUuid);

      List<UUID> exportConfigIds =
          paEndpointUuid == null ? null : Collections.singletonList(paEndpointUuid);
      perfAdvisorService.putUniverse(collector, universe, mode, exportConfigIds);
      UUID endpointUuidToStore = mode.requiresExportConfig() ? paEndpointUuid : null;
      Universe.saveDetails(
          taskParams().getUniverseUUID(),
          u -> {
            u.getUniverseDetails().setPaCollectorUuid(collector.getUuid());
            u.getUniverseDetails().setPaEndpointUuid(endpointUuidToStore);
          });

      log.info("Successfully registered universe {} with PA Collector", universe.getUniverseUUID());
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e);
      throw new RuntimeException(msg, e);
    }
  }
}
