// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeTaskBase;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;

/**
 * Checks TCP port reachability between DB nodes on master/tserver RPC ports using connect_ex from
 * one node to the other. OPEN (something listening) or CLOSED (connection refused: network OK, no
 * listener) is success; UNREACHABLE or FILTERED is failure.
 *
 * <p><b>Forward:</b> Run connect_ex from source node to each target IP:port.
 *
 * <p><b>Reverse:</b> Run connect_ex from target node to source IP:port when source has IP.
 */
@Slf4j
public class CheckDbNodePortConnectivity extends NodeTaskBase {
  private static final long PORT_CHECK_TIMEOUT_SECS = 5;
  private static final String PYTHON3 = "python3";

  public static class Params extends NodeTaskParams {
    /** Node to run the forward check from (source -> target(s)). */
    public NodeDetails sourceNode;

    /**
     * Target nodes. For each target: forward (source -> target) and reverse (target -> source when
     * source has IP). Ports are derived from each target's master/tserver RPC ports.
     */
    public List<NodeDetails> targetNodes = new ArrayList<>();
  }

  @Inject
  protected CheckDbNodePortConnectivity(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    Params p = taskParams();
    String sourceName = p.sourceNode != null ? p.sourceNode.nodeName : p.nodeName;
    int targetCount = p.targetNodes != null ? p.targetNodes.size() : 0;
    return String.format(
        "%s(universeUuid=%s,sourceNode=%s,targetNodes=%d)",
        super.getName(), p.getUniverseUUID(), sourceName, targetCount);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    if (taskParams().sourceNode == null) {
      throw new IllegalArgumentException("requires sourceNode to be set");
    }
    // update source and target node with latest node details from universe details
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails sourceNode = universe.getNode(taskParams().sourceNode.nodeName);
    if (sourceNode == null) {
      throw new IllegalArgumentException("Source node not found in universe details");
    }
    taskParams().sourceNode = sourceNode;
    if (taskParams().targetNodes == null || taskParams().targetNodes.isEmpty()) {
      throw new IllegalArgumentException("requires targetNodes to be non-empty");
    }
    List<NodeDetails> updatedTargetNodes = new ArrayList<>();
    for (NodeDetails targetNode : taskParams().targetNodes) {
      NodeDetails targetNodeDetails = universe.getNode(targetNode.nodeName);
      if (targetNodeDetails == null) {
        throw new IllegalArgumentException("Target node not found in universe details");
      }
      updatedTargetNodes.add(targetNodeDetails);
    }
    taskParams().targetNodes = updatedTargetNodes;
    runMultiTarget(taskParams().sourceNode, universe, taskParams().targetNodes);
  }

  /**
   * One source to multiple targets: for each target, run forward (connect_ex from source to target
   * ports) and reverse (connect_ex from target to source ports when source has IP).
   */
  private void runMultiTarget(
      NodeDetails sourceNode, Universe universe, List<NodeDetails> targetNodes) {
    for (NodeDetails targetNode : targetNodes) {
      if (sourceNode.nodeName.equals(targetNode.nodeName)) {
        continue;
      }
      String targetIp = targetNode.cloudInfo != null ? targetNode.cloudInfo.private_ip : null;
      if (StringUtils.isBlank(targetIp)) {
        log.debug("Skipping target {}: no private IP", targetNode.nodeName);
        continue;
      }
      if (!Util.isIpAddress(targetIp)) {
        throw new IllegalArgumentException(
            String.format(
                "Invalid target IP for connectivity check from %s to %s: %s",
                sourceNode.nodeName, targetNode.nodeName, targetIp));
      }
      String sourceIp = sourceNode.cloudInfo != null ? sourceNode.cloudInfo.private_ip : null;

      List<Integer> targetPorts = getDbPorts(targetNode);
      if (targetPorts.isEmpty()) {
        log.debug("Skipping target {}: no DB RPC ports", targetNode.nodeName);
        continue;
      }

      // Forward: from source, connect_ex to each target IP:port
      for (int port : targetPorts) {
        if (!checkPortReachabilityFromNode(
            sourceNode,
            universe,
            targetIp,
            port,
            sourceNode.nodeName,
            targetNode.nodeName,
            "forward")) {
          throw new RuntimeException(
              String.format(
                  "Port connectivity check failed (forward) from %s (%s) to %s (%s) on port %d",
                  sourceNode.nodeName,
                  StringUtils.isNotBlank(sourceIp) ? sourceIp : "N/A",
                  targetNode.nodeName,
                  targetIp,
                  port));
        }
      }
      log.info(
          "Ports {} are reachable from {} to {} ({})",
          targetPorts,
          sourceNode.nodeName,
          targetNode.nodeName,
          targetIp);

      // Reverse: from target, connect_ex to source IP:port
      if (StringUtils.isNotBlank(sourceIp) && Util.isIpAddress(sourceIp)) {
        List<Integer> sourcePorts = getDbPorts(sourceNode);
        if (!sourcePorts.isEmpty()) {
          for (int port : sourcePorts) {
            if (!checkPortReachabilityFromNode(
                targetNode,
                universe,
                sourceIp,
                port,
                targetNode.nodeName,
                sourceNode.nodeName,
                "reverse")) {
              throw new RuntimeException(
                  String.format(
                      "Port connectivity check failed (reverse) from %s (%s) to %s (%s) on port %d",
                      targetNode.nodeName, targetIp, sourceNode.nodeName, sourceIp, port));
            }
          }
          log.info(
              "Ports {} are reachable from {} to {} ({}) (reverse)",
              sourcePorts,
              targetNode.nodeName,
              sourceNode.nodeName,
              sourceIp);
        }
      }
    }
  }

  private static List<Integer> getDbPorts(NodeDetails node) {
    List<Integer> ports = new ArrayList<>();
    if (node.isMaster) {
      ports.add(node.masterRpcPort);
    }
    if (node.isTserver) {
      ports.add(node.tserverRpcPort);
    }
    return ports;
  }

  /**
   * Runs connect_ex from runOnNode to connectToIp:port. Success = OPEN (0) or CLOSED (111/61);
   * failure = UNREACHABLE, FILTERED, or UNKNOWN.
   */
  private boolean checkPortReachabilityFromNode(
      NodeDetails runOnNode,
      Universe universe,
      String connectToIp,
      int port,
      String fromName,
      String toName,
      String direction) {
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(PORT_CHECK_TIMEOUT_SECS + 5)
            .build();
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              runOnNode, universe, buildPortReachabilityCommand(connectToIp, port), context);
      if (response.isSuccess()) {
        return true;
      }
      log.warn(
          "Port check ({}): {} -> {} ({}):{} : {}",
          direction,
          fromName,
          toName,
          connectToIp,
          port,
          response.getMessage());
    } catch (Exception e) {
      log.warn(
          "Port check ({}): {} -> {} ({}):{} : {}",
          direction,
          fromName,
          toName,
          connectToIp,
          port,
          e.getMessage());
    }
    return false;
  }

  private static final String PORT_CHECK_SCRIPT_RESOURCE =
      "check/check_db_node_port_connectivity.py";

  /**
   * Command that runs connect_ex(host, port) with timeout. Exits 0 for OPEN or CLOSED (network OK);
   * exits 1 for UNREACHABLE, FILTERED, or UNKNOWN. Host and port from argv.
   */
  private List<String> buildPortReachabilityCommand(String host, int port) {
    String code = loadPortCheckScript();
    List<String> command = new ArrayList<>();
    command.add("bash");
    command.add("-c");
    command.add(PYTHON3 + " -c \"" + code.replace("\"", "\\\"") + "\" \"$@\"");
    command.add("_"); // placeholder for $0 in bash -c so host and port go into $@
    command.add(host);
    command.add(String.valueOf(port));
    return command;
  }

  private String loadPortCheckScript() {
    try (InputStream stream = environment.resourceAsStream(PORT_CHECK_SCRIPT_RESOURCE)) {
      if (stream == null) {
        throw new RuntimeException("Port check script not found: " + PORT_CHECK_SCRIPT_RESOURCE);
      }
      String template = IOUtils.toString(stream, StandardCharsets.UTF_8);
      return template.replace("{{TIMEOUT_SECS}}", String.valueOf(PORT_CHECK_TIMEOUT_SECS));
    } catch (IOException e) {
      throw new RuntimeException(
          "Failed to load port check script: " + PORT_CHECK_SCRIPT_RESOURCE, e);
    }
  }
}
