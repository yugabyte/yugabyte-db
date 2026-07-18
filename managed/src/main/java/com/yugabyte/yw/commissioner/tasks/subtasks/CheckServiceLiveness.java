// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.Optional;
import java.util.function.Supplier;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckServiceLiveness extends NodeTaskBase {

  public static class Params extends NodeTaskParams {
    /** Timeout in milliseconds for the liveness check. Default: 5000ms. */
    public long timeoutMs = 5000;
  }

  @Inject
  protected CheckServiceLiveness(BaseTaskDependencies baseTaskDependencies) {
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

    if (node == null) {
      throw new IllegalArgumentException(
          String.format(
              "Node %s not found in universe %s", taskParams().nodeName, universe.getName()));
    }

    log.info(
        "Checking liveness of all services on node {} in universe {}",
        node.nodeName,
        universe.getName());

    StringBuilder failedServices = new StringBuilder();
    boolean hasAnyService = false;

    // Check master liveness if node is configured as master
    if (node.isMaster) {
      hasAnyService = true;
      checkServiceLiveness(
          "MASTER", () -> checkMasterLiveness(universe, node), node, failedServices);
    }

    // Check tserver liveness if node is configured as tserver
    if (node.isTserver) {
      hasAnyService = true;
      checkServiceLiveness(
          "TSERVER", () -> checkTserverLiveness(universe, node), node, failedServices);
    }

    // Check node-agent liveness if node-agent exists for this node
    Optional<NodeAgent> nodeAgentOpt = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (nodeAgentOpt.isPresent()) {
      hasAnyService = true;
      checkServiceLiveness(
          "NODE_AGENT", () -> checkNodeAgentLiveness(universe, node), node, failedServices);
    }

    if (!hasAnyService) {
      log.warn(
          "No services found to check on node {} (isMaster: {}, isTserver: {}, nodeAgent: {})",
          node.nodeName,
          node.isMaster,
          node.isTserver,
          nodeAgentOpt.isPresent());
    }

    if (failedServices.length() > 0) {
      String message =
          String.format(
              "Service(s) %s are not alive on node %s (IP: %s)",
              failedServices.toString(), node.nodeName, node.cloudInfo.private_ip);
      throw new RuntimeException(message);
    }

    log.info("All services are alive on node {} in universe {}", node.nodeName, universe.getName());
  }

  /**
   * Helper method to check service liveness and update failed services list.
   *
   * @param serviceName The name of the service being checked (e.g., "MASTER", "TSERVER").
   * @param livenessCheck A supplier that returns true if the service is alive, false otherwise.
   * @param node The node being checked.
   * @param failedServices StringBuilder to append failed service names to.
   */
  private void checkServiceLiveness(
      String serviceName,
      Supplier<Boolean> livenessCheck,
      NodeDetails node,
      StringBuilder failedServices) {
    try {
      boolean isAlive = livenessCheck.get();
      if (!isAlive) {
        if (failedServices.length() > 0) {
          failedServices.append(", ");
        }
        failedServices.append(serviceName);
      } else {
        log.info("{} service is alive on node {}", serviceName, node.nodeName);
      }
    } catch (Exception e) {
      if (failedServices.length() > 0) {
        failedServices.append(", ");
      }
      failedServices.append(serviceName);
      log.error(
          "{} liveness check failed on node {}: {}", serviceName, node.nodeName, e.getMessage());
    }
  }

  /**
   * Checks if the master service is alive on the given node.
   *
   * @param universe The universe containing the node.
   * @param node The node to check.
   * @return true if the master is alive, false otherwise.
   */
  private boolean checkMasterLiveness(Universe universe, NodeDetails node) {
    String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      log.error("Master addresses are not available for universe {}", universe.getName());
      return false;
    }

    try {
      boolean isAlive = isMasterAliveOnNode(node, masterAddresses);
      log.debug(
          "Master liveness check on node {} ({}:{}) returned: {}",
          node.nodeName,
          node.cloudInfo.private_ip,
          node.masterRpcPort,
          isAlive);
      return isAlive;
    } catch (Exception e) {
      log.error(
          "Exception while checking master liveness on node {}: {}", node.nodeName, e.getMessage());
      return false;
    }
  }

  /**
   * Checks if the tserver service is alive on the given node.
   *
   * @param universe The universe containing the node.
   * @param node The node to check.
   * @return true if the tserver is alive, false otherwise.
   */
  private boolean checkTserverLiveness(Universe universe, NodeDetails node) {
    String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      log.error("Master addresses are not available for universe {}", universe.getName());
      return false;
    }

    try {
      boolean isAlive = isTserverAliveOnNode(node, masterAddresses);
      log.debug(
          "Tserver liveness check on node {} ({}:{}) returned: {}",
          node.nodeName,
          node.cloudInfo.private_ip,
          node.tserverRpcPort,
          isAlive);
      return isAlive;
    } catch (Exception e) {
      log.error(
          "Exception while checking tserver liveness on node {}: {}",
          node.nodeName,
          e.getMessage());
      return false;
    }
  }

  /**
   * Checks if the node-agent service is alive on the given node.
   *
   * @param universe The universe containing the node.
   * @param node The node to check.
   * @return true if the node-agent is alive, false otherwise.
   */
  private boolean checkNodeAgentLiveness(Universe universe, NodeDetails node) {
    String nodeIp = node.cloudInfo.private_ip;
    if (nodeIp == null || nodeIp.isEmpty()) {
      log.error("Node IP is not available for node {}", node.nodeName);
      return false;
    }

    Optional<NodeAgent> nodeAgentOpt = NodeAgent.maybeGetByIp(nodeIp);
    if (!nodeAgentOpt.isPresent()) {
      log.warn("Node agent not found for node {} (IP: {})", node.nodeName, nodeIp);
      return false;
    }

    NodeAgent nodeAgent = nodeAgentOpt.get();
    if (!nodeAgent.isActive()) {
      log.warn(
          "Node agent for node {} (IP: {}) is not in active state. Current state: {}",
          node.nodeName,
          nodeIp,
          nodeAgent.getState());
      return false;
    }

    try {
      // Use a short timeout for the ping to avoid blocking too long
      Duration pingTimeout = Duration.ofMillis(Math.min(taskParams().timeoutMs, 2000));
      nodeAgentClient.waitForServerReady(nodeAgent, pingTimeout);
      log.debug(
          "Node agent liveness check on node {} (IP: {}) returned: true", node.nodeName, nodeIp);
      return true;
    } catch (Exception e) {
      log.warn(
          "Node agent ping failed for node {} (IP: {}): {}", node.nodeName, nodeIp, e.getMessage());
      return false;
    }
  }
}
