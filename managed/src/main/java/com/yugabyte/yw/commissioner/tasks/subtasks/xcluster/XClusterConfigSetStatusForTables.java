// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class XClusterConfigSetStatusForTables extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSetStatusForTables(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.
    // The ids of tables to change their status.
    public Set<String> tableIds;
    // The desired status to put the tables in.
    public XClusterTableConfig.Status desiredStatus;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,desiredStatus=%s)",
        super.getName(), taskParams().getXClusterConfig(), taskParams().desiredStatus);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    try {
      // Save the desired status in the DB.
      xClusterConfig.updateStatusForTables(taskParams().tableIds, taskParams().desiredStatus);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
