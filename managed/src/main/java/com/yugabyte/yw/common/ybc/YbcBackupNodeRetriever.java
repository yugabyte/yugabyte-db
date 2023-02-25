// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.ybc;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.forms.BackupRequestParams.ParallelBackupState;
import com.yugabyte.yw.models.Universe;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;

public class YbcBackupNodeRetriever {
  // For backups in parallel, we move all subtasks to a single subtask group.
  // Initialise a node-ip queue, with the size of queue being equal to parallelism.
  // A consumer picks the node-ip, starts execution.
  // Upto parallel number, this goes on, until no more ips to pick from queue.
  // After completing task, ips returned to pool, so that next subtask blocked on it can pick it up.

  private final LinkedBlockingQueue<String> universeTserverIPs;

  public YbcBackupNodeRetriever(
      UUID universeUUID, int parallelism, Map<String, ParallelBackupState> backupDBStates) {
    universeTserverIPs = new LinkedBlockingQueue<>(parallelism);
    initializeNodePoolForBackups(universeUUID, backupDBStates);
  }

  private void initializeNodePoolForBackups(
      UUID universeUUID, Map<String, ParallelBackupState> backupDBStates) {
    Set<String> nodeIPsAlreadyAssigned =
        backupDBStates
            .entrySet()
            .stream()
            .filter(
                bDBS ->
                    StringUtils.isNotBlank(bDBS.getValue().nodeIp)
                        && !bDBS.getValue().alreadyScheduled)
            .map(bDBS -> bDBS.getValue().nodeIp)
            .collect(Collectors.toSet());
    int nodeIPsToAdd = universeTserverIPs.remainingCapacity() - nodeIPsAlreadyAssigned.size();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    universe
        .getLiveTServersInPrimaryCluster()
        .stream()
        .filter(nD -> !nodeIPsAlreadyAssigned.contains(nD.cloudInfo.private_ip))
        .limit(nodeIPsToAdd)
        .forEach(nD -> universeTserverIPs.add(nD.cloudInfo.private_ip));
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
}
