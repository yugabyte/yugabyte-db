package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteDrConfigEntry extends XClusterConfigTaskBase {

  @Inject
  protected DeleteDrConfigEntry(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  protected DrConfigTaskParams taskParams() {
    return (DrConfigTaskParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format("%s(drConfig=%s)", super.getName(), taskParams().getDrConfig().getUuid());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // Delete the config.
    taskParams().getDrConfig().delete();

    log.info("Completed {}", getName());
  }
}
