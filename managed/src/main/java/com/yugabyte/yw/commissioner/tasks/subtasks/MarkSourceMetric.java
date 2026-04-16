package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.MetricSourceState;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class MarkSourceMetric extends UniverseTaskBase {

  @Inject
  protected MarkSourceMetric(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
    public MetricSourceState metricState;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universe=%s,customer=%s,metricSourceState=%s)",
        this.getClass().getSimpleName(),
        taskParams().getUniverseUUID(),
        taskParams().customerUUID,
        taskParams().metricState);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Customer customer = Customer.getOrBadRequest(taskParams().customerUUID);

    switch (taskParams().metricState) {
      case ACTIVE:
        metricService.markSourceActive(customer.getUuid(), universe.getUniverseUUID());
      case INACTIVE:
        metricService.markSourceInactive(customer.getUuid(), universe.getUniverseUUID());
      default:
        log.debug(
            "No action was taken with marking source metric as: {} for universe: {}",
            taskParams().metricState,
            taskParams().getUniverseUUID());
    }

    log.info("Completed {}", getName());
  }
}
