// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class XClusterConfigSetStatusForNamespaces extends XClusterConfigTaskBase {
  @Inject
  protected XClusterConfigSetStatusForNamespaces(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.
    // The ids of dbs must be stored in dbs field.
    // The desired status to put the dbs in.
    public XClusterNamespaceConfig.Status desiredStatus;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,dbs=%s,desiredStatus=%s)",
        super.getName(),
        taskParams().getXClusterConfig(),
        taskParams().getDbs(),
        taskParams().desiredStatus);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    try {
      // Save the desired status in the DB.
      xClusterConfig.updateStatusForNamespaces(taskParams().dbs, taskParams().desiredStatus);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
    log.info("Completed {}", getName());
  }
}
