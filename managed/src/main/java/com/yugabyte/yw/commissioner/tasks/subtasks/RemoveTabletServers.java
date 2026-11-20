/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CancellationException;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.MasterErrorException;
import org.yb.client.RemoveTabletServerResponse;
import org.yb.client.YBClientApi;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.ServerInfo;

/**
 * Removes the tservers of nodes that are gone for good from the master's tserver registry. Without
 * this the master keeps a dead entry that every tserver keeps pinging. Runs before the nodes are
 * cleared from the server blacklist, which the master requires. It never fails the task: a node
 * whose tserver cannot be removed keeps its dead entry, as before.
 */
@Slf4j
public class RemoveTabletServers extends UniverseTaskBase {

  // First builds whose master has the RemoveTabletServer RPC.
  public static final String MIN_VERSION_REMOVE_TSERVER_STABLE = "2025.1.0.0-b1";
  public static final String MIN_VERSION_REMOVE_TSERVER_PREVIEW = "2.25.1.0-b1";

  // Refusals that clear on their own once the master has gone long enough without a heartbeat.
  private static final List<String> RETRYABLE_REFUSALS =
      List.of("because it is live", "xCluster-guarded information lease");

  private static final Duration RETRY_INTERVAL = Duration.ofSeconds(5);

  @Inject
  protected RemoveTabletServers(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    // The nodes whose tservers are gone for good.
    public Collection<NodeDetails> nodes;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static boolean isSupported(String ybSoftwareVersion) {
    return Util.compareYBVersions(
            ybSoftwareVersion,
            MIN_VERSION_REMOVE_TSERVER_STABLE,
            MIN_VERSION_REMOVE_TSERVER_PREVIEW,
            true)
        >= 0;
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    Map<String, Set<String>> pendingIps = getNodeIps(universe);
    if (pendingIps.isEmpty()) {
      return;
    }
    Duration timeout =
        confGetter.getConfForScope(universe, UniverseConfKeys.ybEditRemoveDeadTserversTimeout);
    long deadline = System.currentTimeMillis() + timeout.toMillis();
    try (YBClientApi client = ybService.getUniverseClient(universe)) {
      while (true) {
        try {
          List<ServerInfo> servers = client.listTabletServers().getTabletServersList();
          pendingIps.entrySet().removeIf(e -> tryRemove(client, e.getKey(), e.getValue(), servers));
        } catch (CancellationException e) {
          throw e;
        } catch (Exception e) {
          log.debug("Failed to list tservers on the master, retrying: {}", e.getMessage());
        }
        if (pendingIps.isEmpty()) {
          return;
        }
        if (System.currentTimeMillis() >= deadline) {
          log.warn(
              "Gave up removing the tservers of nodes {} from the master after {}; they stay"
                  + " registered as dead",
              pendingIps.keySet(),
              timeout);
          return;
        }
        waitFor(RETRY_INTERVAL);
      }
    } catch (CancellationException e) {
      throw e;
    } catch (Exception e) {
      log.warn(
          "Failed to remove the tservers of nodes {} from the master: {}",
          pendingIps.keySet(),
          e.getMessage());
    }
  }

  /**
   * Maps each node to its IPs, skipping nodes without one and on-prem nodes whose instance is
   * DECOMMISSIONED: their servers may still run, so they stay blacklisted and registered.
   */
  private Map<String, Set<String>> getNodeIps(Universe universe) {
    Map<String, Set<String>> nodeIps = new HashMap<>();
    for (NodeDetails node : taskParams().nodes) {
      if (isDecommissionedOnPrem(universe, node)) {
        log.info("Skipping node {}: its on-prem instance is decommissioned", node.getNodeName());
        continue;
      }
      Set<String> ips = new HashSet<>();
      ips.add(Util.getNodeIp(universe, node));
      if (node.cloudInfo != null) {
        ips.add(node.cloudInfo.secondary_private_ip);
      }
      ips.removeIf(StringUtils::isBlank);
      if (!ips.isEmpty()) {
        nodeIps.put(node.getNodeName(), ips);
      }
    }
    return nodeIps;
  }

  private boolean isDecommissionedOnPrem(Universe universe, NodeDetails node) {
    if (Util.getProviderGetter(universe).apply(node).getCloudCode() != Common.CloudType.onprem) {
      return false;
    }
    Optional<NodeInstance> instance = NodeInstance.maybeGet(node.getNodeUuid());
    return instance.isPresent()
        && instance.get().getState().equals(NodeInstance.State.DECOMMISSIONED);
  }

  /**
   * Tries to remove every registry entry whose reported host is one of the node's IPs. An IP reused
   * from an earlier tserver can match more than one entry. Returns false when the node needs
   * another try: the master refused an entry for a reason that clears with time, or the call
   * failed.
   */
  private boolean tryRemove(
      YBClientApi client, String nodeName, Set<String> ips, List<ServerInfo> servers) {
    Set<String> uuids =
        servers.stream()
            .filter(s -> ips.contains(s.getHost()))
            .map(ServerInfo::getUuid)
            .collect(Collectors.toSet());
    boolean done = true;
    for (String uuid : uuids) {
      String refusal;
      try {
        refusal = removeTabletServer(client, uuid);
      } catch (Exception e) {
        log.debug(
            "Failed to remove tserver {} of node {}, retrying: {}", uuid, nodeName, e.getMessage());
        done = false;
        continue;
      }
      if (refusal == null) {
        log.info("Removed tserver {} of node {} from the master", uuid, nodeName);
      } else if (RETRYABLE_REFUSALS.stream().anyMatch(refusal::contains)) {
        log.debug(
            "Master refused to remove tserver {} of node {} yet: {}", uuid, nodeName, refusal);
        done = false;
      } else {
        log.warn(
            "Master refused to remove tserver {} of node {}; it stays registered as dead: {}",
            uuid,
            nodeName,
            refusal);
      }
    }
    return done;
  }

  // Returns null when the tserver is removed or already gone, else the master's refusal.
  private String removeTabletServer(YBClientApi client, String uuid) throws Exception {
    MasterErrorPB error;
    try {
      RemoveTabletServerResponse response = client.removeTabletServer(uuid);
      error = response.getError();
    } catch (MasterErrorException e) {
      if (e.error == null) {
        throw e;
      }
      error = e.error;
    }
    if (error == null || error.getCode() == MasterErrorPB.Code.TABLET_SERVER_NOT_FOUND) {
      return null;
    }
    return error.getStatus().getMessage();
  }
}
