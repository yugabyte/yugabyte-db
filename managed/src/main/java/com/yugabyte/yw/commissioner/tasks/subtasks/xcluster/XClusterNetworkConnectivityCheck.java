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

  // Ping results are informational only (ICMP may be disabled so it doesn't fail the precheck).
  private record PingResult(boolean success, String details) {}

  private record MethodAttempt(String method, boolean success, String details) {}

  private record PortCheckResult(boolean success, List<MethodAttempt> attempts) {}

  private record PortFailure(
      String targetNodeName,
      String sourceNodeName,
      String sourceIp,
      int port,
      List<MethodAttempt> attempts) {}

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

    Duration pingTimeout =
        confGetter.getConfForScope(
            targetUniverse, UniverseConfKeys.xClusterNetworkConnectivityCheckPingCommandTimeout);

    List<PortFailure> portFailures = new ArrayList<>();

    for (NodeDetails sourceNode : sourceNodes) {
      log.info(
          "Checking connectivity from target node {} to source node {}",
          targetNode.nodeName,
          sourceNode.nodeName);

      // Perform ping test (informational only; does NOT fail the precheck).
      PingResult pingResult = checkPing(targetNode, targetUniverse, sourceNode, pingTimeout);
      if (!pingResult.success()) {
        log.warn(
            "Ping failed from {} to {} (ICMP may be disabled). Continuing with TCP port checks."
                + " Details: {}",
            targetNode.nodeName,
            sourceNode.nodeName,
            sanitizeOneLine(pingResult.details()));
      } else {
        log.info("Ping successful from {} to {}", targetNode.nodeName, sourceNode.nodeName);
      }

      // Determine which ports to check based on the source node's roles.
      List<Integer> portsToCheck = new ArrayList<>();
      if (sourceNode.isMaster) {
        portsToCheck.add(sourceNode.masterRpcPort);
      }
      if (sourceNode.isTserver) {
        portsToCheck.add(sourceNode.tserverRpcPort);
      }

      for (Integer port : portsToCheck) {
        PortCheckResult result = checkPort(targetNode, targetUniverse, sourceNode, port);
        if (!result.success()) {
          portFailures.add(
              new PortFailure(
                  targetNode.nodeName,
                  sourceNode.nodeName,
                  sourceNode.cloudInfo.private_ip,
                  port,
                  result.attempts()));

          // Single warning per failed port; details also go into the thrown exception.
          log.warn(
              "Port {} is NOT accessible from {} to {} (attempts: {})",
              port,
              targetNode.nodeName,
              sourceNode.nodeName,
              formatAttemptsOneLine(result.attempts()));
        } else {
          log.info(
              "Port {} is accessible from {} to {}",
              port,
              targetNode.nodeName,
              sourceNode.nodeName);
        }
      }
    }

    if (!portFailures.isEmpty()) {
      throw new RuntimeException(
          "Network connectivity check failed (TCP port connectivity). Port failures: "
              + formatPortFailuresAsArray(portFailures));
    }

    log.info("Completed {}", getName());
  }

  /**
   * Checks basic network connectivity using ping from the target node to the source node.
   *
   * <p>NOTE: Ping (ICMP) failure is treated as informational only because some customer networks
   * disable ICMP while allowing required TCP ports.
   */
  private PingResult checkPing(
      NodeDetails targetNode, Universe targetUniverse, NodeDetails sourceNode, Duration timeout) {
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
            // Keep ping logs quiet; surface details in our warning instead.
            .logCmdOutput(false)
            .timeoutSecs(3 * timeout.getSeconds() + 10)
            .build();

    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(targetNode, targetUniverse, pingCommand, context);
      if (response.isSuccess()) {
        return new PingResult(true, response.getMessage());
      }
      return new PingResult(false, response.getMessage());
    } catch (Exception e) {
      return new PingResult(false, e.getMessage());
    }
  }

  /**
   * Checks if a specific port is accessible from the target node to the source node using multiple
   * methods: nc, telnet, and bash's /dev/tcp.
   */
  private PortCheckResult checkPort(
      NodeDetails targetNode, Universe targetUniverse, NodeDetails sourceNode, int port) {
    List<MethodAttempt> attempts = new ArrayList<>(3);

    MethodAttempt ncAttempt = attemptPortWithNC(targetNode, targetUniverse, sourceNode, port);
    attempts.add(ncAttempt);
    if (ncAttempt.success()) {
      return new PortCheckResult(true, attempts);
    }

    MethodAttempt telnetAttempt =
        attemptPortWithTelnet(targetNode, targetUniverse, sourceNode, port);
    attempts.add(telnetAttempt);
    if (telnetAttempt.success()) {
      return new PortCheckResult(true, attempts);
    }

    MethodAttempt bashAttempt =
        attemptPortWithBashTCP(targetNode, targetUniverse, sourceNode, port);
    attempts.add(bashAttempt);
    if (bashAttempt.success()) {
      return new PortCheckResult(true, attempts);
    }

    return new PortCheckResult(false, attempts);
  }

  private MethodAttempt attemptPortWithNC(
      NodeDetails targetNode, Universe targetUniverse, NodeDetails sourceNode, int port) {
    List<String> ncCommand =
        List.of("nc", "-zv", sourceNode.cloudInfo.private_ip, String.valueOf(port));
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(false).timeoutSecs(5).build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(targetNode, targetUniverse, ncCommand, context);
      return new MethodAttempt("nc", response.isSuccess(), response.getMessage());
    } catch (Exception e) {
      return new MethodAttempt("nc", false, e.getMessage());
    }
  }

  private MethodAttempt attemptPortWithTelnet(
      NodeDetails targetNode, Universe targetUniverse, NodeDetails sourceNode, int port) {
    List<String> telnetCommand =
        List.of("telnet", sourceNode.cloudInfo.private_ip, String.valueOf(port));
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(false).timeoutSecs(5).build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(targetNode, targetUniverse, telnetCommand, context);
      return new MethodAttempt("telnet", response.isSuccess(), response.getMessage());
    } catch (Exception e) {
      return new MethodAttempt("telnet", false, e.getMessage());
    }
  }

  private MethodAttempt attemptPortWithBashTCP(
      NodeDetails targetNode, Universe targetUniverse, NodeDetails sourceNode, int port) {
    String bashCommand =
        String.format("echo > /dev/tcp/%s/%d", sourceNode.cloudInfo.private_ip, port);
    List<String> bashCheckCommand = List.of("bash", "-c", bashCommand);
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(false).timeoutSecs(5).build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(targetNode, targetUniverse, bashCheckCommand, context);
      return new MethodAttempt("bash:/dev/tcp", response.isSuccess(), response.getMessage());
    } catch (Exception e) {
      return new MethodAttempt("bash:/dev/tcp", false, e.getMessage());
    }
  }

  private static String formatAttemptsOneLine(List<MethodAttempt> attempts) {
    StringBuilder sb = new StringBuilder();
    sb.append("[");
    for (int i = 0; i < attempts.size(); i++) {
      MethodAttempt a = attempts.get(i);
      sb.append("{method=").append(a.method()).append(", success=").append(a.success());
      if (!a.success() && a.details() != null && !a.details().isBlank()) {
        sb.append(", details=").append(sanitizeOneLine(a.details()));
      }
      sb.append("}");
      if (i < attempts.size() - 1) {
        sb.append(", ");
      }
    }
    sb.append("]");
    return sb.toString();
  }

  private static String formatPortFailuresAsArray(List<PortFailure> failures) {
    StringBuilder sb = new StringBuilder();
    sb.append("[\n");
    for (int i = 0; i < failures.size(); i++) {
      PortFailure f = failures.get(i);
      sb.append("  {")
          .append("\"target\":\"")
          .append(escapeJson(f.targetNodeName()))
          .append("\",")
          .append("\"source\":\"")
          .append(escapeJson(f.sourceNodeName()))
          .append("\",")
          .append("\"sourceIp\":\"")
          .append(escapeJson(f.sourceIp()))
          .append("\",")
          .append("\"port\":")
          .append(f.port())
          .append(",")
          .append("\"attempts\":")
          .append(formatAttemptsAsJsonArray(f.attempts()))
          .append("}");
      if (i < failures.size() - 1) {
        sb.append(",");
      }
      sb.append("\n");
    }
    sb.append("]");
    return sb.toString();
  }

  private static String formatAttemptsAsJsonArray(List<MethodAttempt> attempts) {
    StringBuilder sb = new StringBuilder();
    sb.append("[");
    for (int i = 0; i < attempts.size(); i++) {
      MethodAttempt a = attempts.get(i);
      sb.append("{")
          .append("\"method\":\"")
          .append(escapeJson(a.method()))
          .append("\",")
          .append("\"success\":")
          .append(a.success());
      if (a.details() != null && !a.details().isBlank()) {
        sb.append(",\"details\":\"").append(escapeJson(sanitizeOneLine(a.details()))).append("\"");
      }
      sb.append("}");
      if (i < attempts.size() - 1) {
        sb.append(",");
      }
    }
    sb.append("]");
    return sb.toString();
  }

  private static String sanitizeOneLine(String s) {
    if (s == null) {
      return "";
    }
    String v = s.replace("\r", " ").replace("\n", " ").trim();
    // Avoid massive exception messages from command output.
    int max = 800;
    return v.length() <= max ? v : v.substring(0, max) + "...(truncated)";
  }

  private static String escapeJson(String s) {
    if (s == null) {
      return "";
    }
    return s.replace("\\", "\\\\").replace("\"", "\\\"");
  }
}
