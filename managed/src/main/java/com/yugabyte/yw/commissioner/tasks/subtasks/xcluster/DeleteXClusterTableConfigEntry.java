package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** It is the subtask to delete the xCluster table config entry from the Platform DB. */
@Slf4j
public class DeleteXClusterTableConfigEntry extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterTableConfigEntry(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.

    // The list of tables to delete from the xCluster config object.
    public Set<String> tableIds;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s)", super.getName(), taskParams().getXClusterConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    xClusterConfig.removeTables(taskParams().tableIds);

    log.info("Completed {}", getName());
  }
}
