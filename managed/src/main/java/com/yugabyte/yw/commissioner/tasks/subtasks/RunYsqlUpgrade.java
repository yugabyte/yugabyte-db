/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RunYsqlUpgrade extends AbstractTaskBase {

  private static final String MIN_YSQL_UPGRADE_RELEASE = "2.8.0.0";
  private static final String NO_YSQL_UPGRADE_RELEASE = "2.9.0.0";
  private static final String NEXT_YSQL_UPGRADE_RELEASE = "2.11.0.0";
  private static final long TIMEOUT_SEC = TimeUnit.MINUTES.toSeconds(3);

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  protected RunYsqlUpgrade(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  // Parameters for YSQL upgrade task.
  public static class Params extends UniverseTaskParams {
    public String ybSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    if (!CommonUtils.isReleaseEqualOrAfter(
        MIN_YSQL_UPGRADE_RELEASE, taskParams().ybSoftwareVersion)) {
      log.info("Skipping YSQL upgrade as current YB version is {}", taskParams().ybSoftwareVersion);
      return;
    }
    if (CommonUtils.isReleaseBetween(
        NO_YSQL_UPGRADE_RELEASE, NEXT_YSQL_UPGRADE_RELEASE, taskParams().ybSoftwareVersion)) {
      log.info("Skipping YSQL upgrade as current YB version is {}", taskParams().ybSoftwareVersion);
      return;
    }
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();

    if (!primaryCluster.userIntent.enableYSQL) {
      log.info("Skipping YSQL upgrade as the universe isn't configured for YSQL.");
      return;
    }

    final String leaderMasterAddress = universe.getMasterLeaderHostText();
    NodeDetails leaderMasterNode =
        universe
            .getUniverseDetails()
            .getNodesInCluster(primaryCluster.uuid)
            .stream()
            .filter(nodeDetails -> nodeDetails.isMaster)
            .filter(nodeDetails -> leaderMasterAddress.equals(nodeDetails.cloudInfo.private_ip))
            .findFirst()
            .orElseThrow(
                () ->
                    new IllegalStateException(
                        "Failed to find leader master node " + leaderMasterAddress));

    ShellResponse response =
        nodeUniverseManager.runYbAdminCommand(
            leaderMasterNode, universe, "upgrade_ysql", TIMEOUT_SEC);
    processShellResponse(response);

    log.info("Successfully performed YSQL upgrade");
  }
}
