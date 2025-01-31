// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PGUpgradeTServerCheck extends ServerSubTaskBase {

  private final NodeUniverseManager nodeUniverseManager;

  private final int PG_UPGRADE_CHECK_TIMEOUT = 300;

  public static class Params extends ServerSubTaskParams {
    public String ybSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected PGUpgradeTServerCheck(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    List<NodeDetails> tServerNodes = universe.getTServers();

    // Run the check on all the tServer nodes and fail the task only if it fails on all nodes.
    boolean success = false;
    for (NodeDetails node : tServerNodes) {
      try {
        runCheckOnNode(universe, node);
        success = true;
        break;
      } catch (RuntimeException e) {
        log.error("Error while running PG15 upgrade check on node: " + node.nodeName, e);
      }
    }

    if (!success) {
      throw new RuntimeException(
          "PG15 upgrade check failed on all tServer nodes. Check logs for more info.");
    }
  }

  private void runCheckOnNode(Universe universe, NodeDetails node) {
    List<String> command = new ArrayList<>();
    Architecture arch = universe.getUniverseDetails().arch;
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    String ybServerPackage = release.getFilePath(arch);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String pgUpgradeBinaryLocation =
        nodeUniverseManager.getYbHomeDir(node, universe)
            + "/yb-software/"
            + extractVersionDir(ybServerPackage)
            + "/postgres/bin/pg_upgrade";
    command.add(pgUpgradeBinaryLocation);
    command.add("-d");
    String pgDataDir = Util.getDataDirectoryPath(universe, node, config) + "/pg_data";
    command.add(pgDataDir);
    command.add("--old-host");
    if (primaryCluster.userIntent.enableYSQLAuth) {
      String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
      command.add(String.format("'$(ls -d -t %s/.yb.* | head -1)'", customTmpDirectory));
    } else {
      command.add(node.cloudInfo.private_ip);
    }
    command.add("--old-port");
    if (primaryCluster.userIntent.enableConnectionPooling) {
      command.add(String.valueOf(node.internalYsqlServerRpcPort));
    } else {
      command.add(String.valueOf(node.ysqlServerRpcPort));
    }
    command.add("--username");
    command.add("\"yugabyte\"");
    command.add("--check");

    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(PG_UPGRADE_CHECK_TIMEOUT)
            .build();

    log.info("Running PG15 upgrade check on node: {} with command: ", node.nodeName, command);
    ShellResponse response =
        nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
    if (response.code != 0) {
      log.info(
          "PG upgrade check failed on node: {} with error: {}", node.nodeName, response.message);
      throw new RuntimeException("PG15 upgrade check failed on node: " + node.nodeName);
    }
  }

  private String extractVersionDir(String ybServerPackage) {
    String[] parts = ybServerPackage.split("/");
    String tarPackageName = parts[parts.length - 1];
    return tarPackageName.replace(".tar.gz", "");
  }
}
