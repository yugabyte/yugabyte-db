// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckMemory extends UniverseTaskBase {

  private final NodeUniverseManager nodeUniverseManager;

  private final int DEFAULT_COMMAND_TIMEOUT_SEC = 20;

  @Inject
  protected CheckMemory(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public static class Params extends UniverseTaskParams {
    public Long memoryLimitKB;
    public String memoryType;
    public List<String> nodeIpList;
  }

  protected CheckMemory.Params params() {
    return (CheckMemory.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      Universe universe = getUniverse();
      for (String nodeIp : params().nodeIpList) {
        List<String> command = new ArrayList<>();
        command.add("grep");
        command.add("-i");
        command.add(params().memoryType);
        command.add("/proc/meminfo");
        command.add("|");
        command.add("awk");
        command.add("{print $2}");
        NodeDetails node = universe.getNodeByPrivateIP(nodeIp);
        ShellProcessContext context =
            ShellProcessContext.builder()
                .logCmdOutput(true)
                .timeoutSecs(DEFAULT_COMMAND_TIMEOUT_SEC)
                .build();
        ShellResponse response =
            nodeUniverseManager.runCommand(node, universe, command, context).processErrors();

        long availMemory = Long.parseLong(response.extractRunCommandOutput());
        if (availMemory < params().memoryLimitKB) {
          throw new RuntimeException(
              "Insufficient memory available on node "
                  + nodeIp
                  + " as "
                  + params().memoryLimitKB
                  + " is required but found "
                  + availMemory);
        }
      }
      log.info("Validated Enough memory is available for Upgrade.");
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      Throwables.propagate(e);
    }
  }
}
