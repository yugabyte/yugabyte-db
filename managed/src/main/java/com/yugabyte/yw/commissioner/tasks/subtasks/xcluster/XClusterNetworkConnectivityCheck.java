/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeTaskBase;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class XClusterNetworkConnectivityCheck extends NodeTaskBase {

  @Inject
  protected XClusterNetworkConnectivityCheck(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // The universe UUID of the target universe.
    // The name of the node to check the connectivity against the source universe must be stored in
    // nodeName field.

    public XClusterConfig xClusterConfig;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s,nodeName=%s,xClusterConfig=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().nodeName,
        taskParams().xClusterConfig);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    NodeDetails targetNode = targetUniverse.getNode(taskParams().nodeName);
    if (targetNode == null) {
      throw new IllegalArgumentException(
          String.format(
              "Node with name %s in universe %s not found",
              taskParams().nodeName, taskParams().getUniverseUUID()));
    }
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Collection<NodeDetails> sourceNodes = sourceUniverse.getNodes();

    boolean allConnected = true;

    Duration pingTimeout =
        confGetter.getConfForScope(
            targetUniverse, UniverseConfKeys.xClusterNetworkConnectivityCheckPingCommandTimeout);

    for (NodeDetails sourceNode : sourceNodes) {
      log.info(
          "Checking connectivity from target node {} to source node {}",
          targetNode.nodeName,
          sourceNode.nodeName);

      // Perform ping test.
      boolean pingSuccess = checkPing(targetNode, sourceNode, pingTimeout);
      if (!pingSuccess) {
        log.error("Ping failed from {} to {}", targetNode.nodeName, sourceNode.nodeName);
        allConnected = false;
        continue; // Skip port checks if ping fails
      }
      log.info("Ping successful from {} to {}", targetNode.nodeName, sourceNode.nodeName);

      // Determine which ports to check based on the source node's roles.
      List<Integer> portsToCheck = new ArrayList<>();

      if (sourceNode.isMaster) {
        portsToCheck.add(sourceNode.masterRpcPort);
      }
      if (sourceNode.isTserver) {
        portsToCheck.add(sourceNode.tserverRpcPort);
      }

      for (Integer port : portsToCheck) {
        boolean portSuccess = checkPort(targetNode, sourceNode, port);
        if (!portSuccess) {
          log.error(
              "Port {} is not accessible from {} to {}",
              port,
              targetNode.nodeName,
              sourceNode.nodeName);
          allConnected = false;
        } else {
          log.info(
              "Port {} is accessible from {} to {}",
              port,
              targetNode.nodeName,
              sourceNode.nodeName);
        }
      }
    }

    if (!allConnected) {
      throw new RuntimeException(
          "Network connectivity check failed between target and source universe nodes.");
    }

    log.info("Completed {}", getName());
  }

  /**
   * Checks basic network connectivity using ping from the target node to the source node.
   *
   * @param targetNode The node from which the ping will be initiated.
   * @param sourceNode The node to which the ping will be sent.
   * @param timeout The duration for which the ping command will wait for a response.
   * @return true if ping is successful, false otherwise.
   */
  private boolean checkPing(NodeDetails targetNode, NodeDetails sourceNode, Duration timeout) {
    List<String> pingCommand =
        List.of(
            "ping",
            "-c",
            "3",
            "-W",
            String.valueOf(timeout.getSeconds()),
            sourceNode.cloudInfo.private_ip);
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(3 * timeout.getSeconds() + 10)
            .build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              targetNode,
              Universe.getOrBadRequest(taskParams().xClusterConfig.getTargetUniverseUUID()),
              pingCommand,
              context);
      return response.isSuccess();
    } catch (Exception e) {
      log.error("Ping command failed: {}", e.getMessage());
      return false;
    }
  }

  /**
   * Checks if a specific port is accessible from the target node to the source node using multiple
   * methods: nc, telnet, and bash's /dev/tcp.
   *
   * @param targetNode The node from which the port check will be initiated.
   * @param sourceNode The node to which the port will be checked.
   * @param port The port number to check.
   * @return true if the port is accessible using any method, false otherwise.
   */
  private boolean checkPort(NodeDetails targetNode, NodeDetails sourceNode, int port) {
    // Attempt using nc (netcat).
    if (checkPortWithNC(targetNode, sourceNode, port)) {
      log.debug(
          "Port {} accessible via nc from {} to {}",
          port,
          targetNode.nodeName,
          sourceNode.nodeName);
      return true;
    }

    // Attempt using telnet.
    if (checkPortWithTelnet(targetNode, sourceNode, port)) {
      log.debug(
          "Port {} accessible via telnet from {} to {}",
          port,
          targetNode.nodeName,
          sourceNode.nodeName);
      return true;
    }

    // Attempt using bash's /dev/tcp.
    if (checkPortWithBashTCP(targetNode, sourceNode, port)) {
      log.debug(
          "Port {} accessible via bash /dev/tcp from {} to {}",
          port,
          targetNode.nodeName,
          sourceNode.nodeName);
      return true;
    }

    log.error(
        "Port {} is not accessible via any method from {} to {}",
        port,
        targetNode.nodeName,
        sourceNode.nodeName);
    return false;
  }

  /**
   * Attempts to check port connectivity using nc (netcat).
   *
   * @param targetNode The node from which the port check will be initiated.
   * @param sourceNode The node to which the port will be checked.
   * @param port The port number to check.
   * @return true if the port is accessible via nc, false otherwise.
   */
  private boolean checkPortWithNC(NodeDetails targetNode, NodeDetails sourceNode, int port) {
    List<String> ncCommand =
        List.of("nc", "-zv", sourceNode.cloudInfo.private_ip, String.valueOf(port));
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(5).build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              targetNode,
              Universe.getOrBadRequest(taskParams().xClusterConfig.getTargetUniverseUUID()),
              ncCommand,
              context);
      if (response.isSuccess()) {
        return true;
      } else {
        log.warn("nc command failed for port {}: {}", port, response.getMessage());
        return false;
      }
    } catch (Exception e) {
      log.warn("nc command execution failed for port {}: {}", port, e.getMessage());
      return false;
    }
  }

  /**
   * Attempts to check port connectivity using telnet.
   *
   * @param targetNode The node from which the port check will be initiated.
   * @param sourceNode The node to which the port will be checked.
   * @param port The port number to check.
   * @return true if the port is accessible via telnet, false otherwise.
   */
  private boolean checkPortWithTelnet(NodeDetails targetNode, NodeDetails sourceNode, int port) {
    List<String> telnetCommand =
        List.of("telnet", sourceNode.cloudInfo.private_ip, String.valueOf(port));
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(5).build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              targetNode,
              Universe.getOrBadRequest(taskParams().xClusterConfig.getTargetUniverseUUID()),
              telnetCommand,
              context);
      if (response.isSuccess()) {
        return true;
      } else {
        log.warn("telnet command failed for port {}: {}", port, response.getMessage());
        return false;
      }
    } catch (Exception e) {
      log.warn("telnet command execution failed for port {}: {}", port, e.getMessage());
      return false;
    }
  }

  /**
   * Attempts to check port connectivity using bash's /dev/tcp.
   *
   * @param targetNode The node from which the port check will be initiated.
   * @param sourceNode The node to which the port will be checked.
   * @param port The port number to check.
   * @return true if the port is accessible via bash /dev/tcp, false otherwise.
   */
  private boolean checkPortWithBashTCP(NodeDetails targetNode, NodeDetails sourceNode, int port) {
    // Using bash to attempt opening /dev/tcp/host/port
    String bashCommand =
        String.format("echo > /dev/tcp/%s/%d", sourceNode.cloudInfo.private_ip, port);
    List<String> bashCheckCommand = List.of("bash", "-c", bashCommand);
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(5).build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              targetNode,
              Universe.getOrBadRequest(taskParams().xClusterConfig.getTargetUniverseUUID()),
              bashCheckCommand,
              context);
      if (response.isSuccess()) {
        return true;
      } else {
        log.warn("Bash TCP check command failed for port {}: {}", port, response.getMessage());
        return false;
      }
    } catch (Exception e) {
      log.warn("Bash TCP check execution failed for port {}: {}", port, e.getMessage());
      return false;
    }
  }
}
