package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Date;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetRestoreTime extends XClusterConfigTaskBase {

  @Inject
  protected SetRestoreTime(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The source universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Table ids to set restore time for.
    public Set<String> tableIds;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (sourceUniverse=%s, xClusterUuid=%s, tableIds=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().getXClusterConfig().getUuid(),
        taskParams().tableIds);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // The restore must belong to a parent xCluster config.
    XClusterConfig xClusterConfig = taskParams().getXClusterConfig();
    if (xClusterConfig == null) {
      throw new RuntimeException(
          "taskParams().xClusterConfig is null. Each SetRestoreTime subtask must belong to an "
              + "xCluster config");
    }

    // Update the DB.
    Date now = new Date();
    xClusterConfig.updateRestoreTimeForTables(taskParams().tableIds, now, getTaskUUID());
    log.info("Restore time for tables {} set to {}", taskParams().tableIds, now);

    log.info("Completed {}", getName());
  }
}
