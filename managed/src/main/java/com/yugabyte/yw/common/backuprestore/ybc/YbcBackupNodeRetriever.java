// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.forms.BackupTableParams.ParallelBackupState;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class YbcBackupNodeRetriever {
  // For backups in parallel, we move all subtasks to a single subtask group.
  // Initialise a node-ip queue, with the size of queue being equal to parallelism.
  // A consumer picks the node-ip, starts execution.
  // Upto parallel number, this goes on, until no more ips to pick from queue.
  // After completing task, ips returned to pool, so that next subtask blocked on it can pick it up.

  private final LinkedBlockingQueue<String> universeTserverIPs;
  private final Universe universe;
  private final YbcManager ybcManager;

  public YbcBackupNodeRetriever(Universe universe, int parallelism) {
    this.universeTserverIPs = new LinkedBlockingQueue<>(parallelism);
    this.universe = universe;
    this.ybcManager = StaticInjectorHolder.injector().instanceOf(YbcManager.class);
  }

  public void initializeNodePoolForBackups(Map<UUID, ParallelBackupState> backupDBStates) {
    Set<String> nodeIPsAlreadyAssigned =
        backupDBStates.entrySet().stream()
            .filter(
                bDBS ->
                    StringUtils.isNotBlank(bDBS.getValue().nodeIp)
                        && !bDBS.getValue().alreadyScheduled)
            .map(bDBS -> bDBS.getValue().nodeIp)
            .collect(Collectors.toSet());
    int nodeIPsToAdd = universeTserverIPs.remainingCapacity() - nodeIPsAlreadyAssigned.size();
    if (nodeIPsToAdd == 0) {
      log.debug("Nodes already assigned for backup task.");
    }
    if (nodeIPsToAdd != 0) {
      addUniverseTserverIPs(nodeIPsAlreadyAssigned, nodeIPsToAdd);
      if (universeTserverIPs.size() == 0) {
        throw new RuntimeException("YB-Controller servers unavailable.");
      }
      if (universeTserverIPs.size() < nodeIPsToAdd) {
        log.warn(
            "Found unhealthy nodes, using fewer YB-Controller"
                + " orchestrators: {} than desired parallelism.",
            nodeIPsAlreadyAssigned.size() + universeTserverIPs.size());
      }
    }
  }

  public String getNodeIpForBackup() {
    try {
      return universeTserverIPs.take();
    } catch (InterruptedException e) {
      throw new CancellationException("Aborted while waiting for YBC Orchestrator node-ip.");
    }
  }

  public void putNodeIPBackToPool(String nodeIP) {
    universeTserverIPs.add(nodeIP);
  }

  @VisibleForTesting
  protected String peekNodeIpForBackup() {
    return universeTserverIPs.peek();
  }

  private void addUniverseTserverIPs(Set<String> nodeIPsAlreadyAssigned, int nodeIPsToAdd) {
    int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String certFile = universe.getCertificateNodetoNode();
    List<String> nodeIPs =
        YbcManager.getPreferenceBasedYBCNodeIPsList(universe, nodeIPsAlreadyAssigned);
    nodeIPs.stream()
        .filter(ip -> ybcManager.ybcPingCheck(ip, certFile, ybcPort))
        .limit(nodeIPsToAdd)
        .forEach(ip -> universeTserverIPs.add(ip));
    log.info("Node IPs list for backup: {}", universeTserverIPs.toString());
  }
}
