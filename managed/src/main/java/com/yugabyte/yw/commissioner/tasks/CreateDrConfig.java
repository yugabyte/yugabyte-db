// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class CreateDrConfig extends CreateXClusterConfig {

  @Inject
  protected CreateDrConfig(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, xClusterUniverseService, operatorStatusUpdaterFactory);
  }

  @Override
  protected DrConfigTaskParams taskParams() {
    return (DrConfigTaskParams) taskParams;
  }

  @Override
  public String getName() {
    if (taskParams().getDrConfig() != null) {
      return String.format(
          "%s(uuid=%s, universe=%s)",
          this.getClass().getSimpleName(),
          taskParams().getDrConfig().getUuid(),
          taskParams().getUniverseUUID());
    } else {
      return String.format(
          "%s(universe=%s)", this.getClass().getSimpleName(), taskParams().getUniverseUUID());
    }
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    super.run();

    log.info("Completed {}", getName());
  }
}
