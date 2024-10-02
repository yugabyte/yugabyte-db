// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RollbackYsqlMajorVersionCatalogUpgrade extends UniverseTaskBase {

  private final NodeUniverseManager nodeUniverseManager;

  private final long TIMEOUT = 600000;

  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected RollbackYsqlMajorVersionCatalogUpgrade(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  @Override
  public void run() {

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails masterLeaderNode = universe.getMasterLeaderNode();

    ShellResponse response =
        nodeUniverseManager.runYbAdminCommand(
            masterLeaderNode,
            universe,
            taskInfo,
            Collections.singletonList("rollback_ysql_major_version_upgrade"),
            TIMEOUT);

    if (response.code != 0) {
      log.error("Failed to run rollback_ysql_major_version_upgrade. Error: " + response.message);
      throw new RuntimeException(
          "Failed to run rollback_ysql_major_version_upgrade. Check logs for more info.");
    }

    log.info("Successfully ran rollback_ysql_major_version_upgrade.");
  }
}
