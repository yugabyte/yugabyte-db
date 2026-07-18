// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckNodeCommandExecution extends NodeTaskBase {

  public static class Params extends NodeTaskParams {
    /** Timeout in seconds for the command execution check. Default: 10 seconds. */
    public long timeoutSecs = 10;

    public NodeUniverseManager nodeUniverseManager;
  }

  @Inject
  protected CheckNodeCommandExecution(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s,nodeName=%s)",
        super.getName(), taskParams().getUniverseUUID(), taskParams().nodeName);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType
        == Common.CloudType.local) {
      log.info("Skipping check for local provider");
      return;
    }

    if (node == null) {
      throw new IllegalArgumentException(
          String.format(
              "Node %s not found in universe %s", taskParams().nodeName, universe.getName()));
    }

    log.info(
        "Checking command execution capability on node {} in universe {}",
        node.nodeName,
        universe.getName());

    boolean canExecuteCommands = checkCommandExecution(universe, node);

    if (!canExecuteCommands) {
      String message =
          String.format(
              "Cannot execute commands on node %s (IP: %s). Node may be unreachable or"
                  + " SSH/node-agent connection may be unavailable.",
              node.nodeName, node.cloudInfo.private_ip);
      throw new RuntimeException(message);
    }

    log.info(
        "Command execution check passed on node {} in universe {}",
        node.nodeName,
        universe.getName());
  }

  /**
   * Checks if commands can be executed on the given node by running a simple test command.
   *
   * @param universe The universe containing the node.
   * @param node The node to check.
   * @return true if commands can be executed, false otherwise.
   */
  private boolean checkCommandExecution(Universe universe, NodeDetails node) {
    // Run a simple command to verify command execution capability
    // Using 'echo' with a known value to verify we can execute and get output
    List<String> testCommand = Arrays.asList("echo", "command-execution-test");

    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs((int) taskParams().timeoutSecs)
            .build();

    try {
      ShellResponse response = nodeUniverseManager.runCommand(node, universe, testCommand, context);
      if (response.isSuccess()) {
        String output = response.extractRunCommandOutput().trim();
        boolean isValid = output.equals("command-execution-test");
        log.debug(
            "Command execution check on node {} returned: {} (output: {})",
            node.nodeName,
            isValid,
            output);
        return isValid;
      } else {
        log.error(
            "Command execution check failed on node {}: {}", node.nodeName, response.getMessage());
        return false;
      }
    } catch (Exception e) {
      log.error(
          "Exception while checking command execution on node {}: {}",
          node.nodeName,
          e.getMessage());
      return false;
    }
  }
}
