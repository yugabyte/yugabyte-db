/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.common.pa.PerfAdvisorEndpointService;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Puts a Perf Advisor Endpoint on the collector as its export config, before the universe that
 * needs it is registered - the collector rejects an online registration that names a destination it
 * has never heard of.
 *
 * <p>Separate from the registration subtask because its failures are a different kind: an
 * unreachable destination, a name a Perf Advisor operator already used, a rejected credential. The
 * task drawer should say which of the two steps failed.
 */
@Slf4j
public class PushPaExportConfig extends UniverseTaskBase {

  private final PerfAdvisorService perfAdvisorService;
  private final PerfAdvisorEndpointService endpointService;

  @Inject
  protected PushPaExportConfig(
      BaseTaskDependencies baseTaskDependencies,
      PerfAdvisorService perfAdvisorService,
      PerfAdvisorEndpointService endpointService) {
    super(baseTaskDependencies);
    this.perfAdvisorService = perfAdvisorService;
    this.endpointService = endpointService;
  }

  public static class Params extends UniverseTaskParams {
    public UUID paCollectorUuid;
    public UUID paEndpointUuid;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Customer customer = Customer.get(universe.getCustomerId());

      PACollector collector =
          perfAdvisorService.getOrBadRequest(customer.getUuid(), taskParams().paCollectorUuid);
      PerfAdvisorEndpoint endpoint =
          endpointService.getOrBadRequest(customer.getUuid(), taskParams().paEndpointUuid);

      endpointService.push(collector, endpoint);
      log.info(
          "Pushed Perf Advisor Endpoint {} to collector {}",
          endpoint.getUuid(),
          collector.getUuid());
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e);
      throw new RuntimeException(msg, e);
    }
  }
}
