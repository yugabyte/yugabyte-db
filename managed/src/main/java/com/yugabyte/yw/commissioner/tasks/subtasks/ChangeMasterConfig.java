/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicInteger;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;

@Slf4j
public class ChangeMasterConfig extends UniverseTaskBase {

  private static final int WAIT_FOR_CHANGE_COMPLETED_INITIAL_DELAY_MILLIS = 1000;
  private static final int WAIT_FOR_CHANGE_COMPLETED_MAX_DELAY_MILLIS = 20000;
  private static final int WAIT_FOR_CHANGE_COMPLETED_MAX_ERRORS = 5;

  private static final Duration YBCLIENT_ADMIN_OPERATION_TIMEOUT = Duration.ofMinutes(15);
  private final YbClientConfigFactory ybcClientConfigFactory;

  @Inject
  protected ChangeMasterConfig(
      BaseTaskDependencies baseTaskDependencies, YbClientConfigFactory ybcConfigFactory) {
    super(baseTaskDependencies);
    this.ybcClientConfigFactory = ybcConfigFactory;
  }

  // Create an enum specifying the operation type.
  public enum OpType {
    AddMaster,
    RemoveMaster
  }

  // Parameters for change master config task.
  public static class Params extends NodeTaskParams {
    // When AddMaster, the master is added to the current master quorum, otherwise it is deleted.
    public OpType opType;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().nodeName
        + ", "
        + taskParams().opType.toString()
        + ")";
  }

  @Override
  public String toString() {
    return getName();
  }

  @Override
  public void run() {
    // Get the master addresses.
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    log.info(
        "Running {}: universe = {}, masterAddress = {}",
        getName(),
        taskParams().getUniverseUUID(),
        masterAddresses);
    if (StringUtils.isBlank(masterAddresses)) {
      throw new IllegalStateException(
          "No master host/ports for a change config op in " + taskParams().getUniverseUUID());
    }

    // Get the node details and perform the change config operation.
    NodeDetails node = universe.getNode(taskParams().nodeName);
    boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
    // If the cluster has a secondary IP, we want to ensure that we use the correct addresses.
    // The ipToUse is the address that we need to add to the config.
    boolean hasSecondaryIp =
        node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    boolean shouldUseSecondary =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("false");
    String ipToUse =
        hasSecondaryIp && shouldUseSecondary
            ? node.cloudInfo.secondary_private_ip
            : node.cloudInfo.private_ip;
    String certificate = universe.getCertificateNodetoNode();
    YbClientConfig config = ybcClientConfigFactory.create(masterAddresses, certificate);
    // The call changeMasterConfig is not idempotent. The client library internally keeps retrying
    // for a long time until it gives up if the node is already added or removed.
    // This optional check ensures that changeMasterConfig is not invoked if the operation is
    // already done for the node.
    if (isChangeMasterConfigDone(universe, node, isAddMasterOp, ipToUse)) {
      log.info(
          "Config change (add={}) is already done for node {}({}:{})",
          isAddMasterOp,
          node.nodeName,
          node.cloudInfo.private_ip,
          node.masterRpcPort);
      if (isAddMasterOp) {
        // Even if it's returned as one of the masters - it can be not bootstrapped yet.
        waitForChangeToComplete(config, ipToUse);
      }
      return;
    }
    // The param useHostPort should be true when removing a dead master.
    // Otherwise, ybclient will attempt to fetch the UUID of the master being changed.
    // Add operation always requires useHostPort=false.
    boolean useHostPort = false;
    if (!isAddMasterOp) {
      // True if master server is not alive else false.
      // TODO Use isMasterAliveOnNode once isMaster is set to true for all tasks.
      // AddNodeToUniverse does not set it to true when this is called.
      useHostPort = !isServerAlive(node, ServerType.MASTER, masterAddresses);
    }
    ChangeConfigResponse response = null;
    config.setAdminOperationTimeout(YBCLIENT_ADMIN_OPERATION_TIMEOUT);
    YBClient client = ybService.getClientWithConfig(config);
    try {
      log.info(
          "Starting changeMasterConfig({}:{}, op={}, useHost={})",
          node.cloudInfo.private_ip,
          node.masterRpcPort,
          taskParams().opType,
          useHostPort);
      response =
          client.changeMasterConfig(
              node.cloudInfo.private_ip, node.masterRpcPort, isAddMasterOp, useHostPort, ipToUse);
    } catch (Exception e) {
      String msg =
          String.format(
              "Error while performing master change config on node %s (%s:%d) - %s",
              node.nodeName, ipToUse, node.masterRpcPort, e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
    // If there was an error, throw an exception.
    if (response != null && response.hasError()) {
      String msg =
          String.format(
              "ChangeConfig response has error for node %s (%s:%d) - %s",
              node.nodeName, ipToUse, node.masterRpcPort, response.errorMessage());
      log.error(msg);
      throw new RuntimeException(msg);
    }

    waitForChangeToComplete(config, ipToUse);
  }

  private void waitForChangeToComplete(YbClientConfig clientConfig, String masterIp) {

    boolean waitForMasterConfigChangeCheckEnabled =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.changeMasterConfigCheckEnabled);
    if (!waitForMasterConfigChangeCheckEnabled) {
      log.debug("Wait for master config complete is disabled");
      return;
    }
    Duration maxWaitTime =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.changeMasterConfigCheckTimeout);
    YBClient client = ybService.getClientWithConfig(clientConfig);
    try {
      AtomicInteger maxErrorCount = new AtomicInteger(WAIT_FOR_CHANGE_COMPLETED_MAX_ERRORS);
      boolean success =
          doWithExponentialTimeout(
              WAIT_FOR_CHANGE_COMPLETED_INITIAL_DELAY_MILLIS,
              WAIT_FOR_CHANGE_COMPLETED_MAX_DELAY_MILLIS,
              maxWaitTime.toMillis(),
              () -> {
                try {
                  // Verify that there is only one master leader
                  // (otherwise this method throws an exception)
                  if (client.getLeaderMasterHostAndPort() == null) {
                    throw new IllegalStateException(
                        "No master leader between masters " + clientConfig.getMasterHostPorts());
                  }
                  ListMastersResponse resp = client.listMasters();
                  ServerInfo serverInfo =
                      resp.getMasters().stream()
                          .filter(s -> masterIp.equals(s.getHost()))
                          .findFirst()
                          .orElse(null);
                  log.debug("Master status: {}", serverInfo);
                  CommonTypes.PeerRole peerRole =
                      serverInfo == null ? null : serverInfo.getPeerRole();
                  if (taskParams().opType == OpType.AddMaster) {
                    return peerRole == CommonTypes.PeerRole.LEADER
                        || peerRole == CommonTypes.PeerRole.FOLLOWER;
                  } else {
                    return peerRole == null;
                  }
                } catch (Exception e) {
                  log.error("Failed to check master peer states", e);
                  if (maxErrorCount.getAndDecrement() == 0) {
                    throw new RuntimeException(e);
                  }
                  return false;
                }
              });
      if (!success) {
        throw new RuntimeException(
            taskParams().opType.name() + " operation has not completed within " + maxWaitTime);
      }
    } catch (Exception e) {
      String msg =
          String.format(
              "Error while checking master peer statuses at %s: %s",
              clientConfig.getMasterHostPorts(), e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, clientConfig.getMasterHostPorts());
    }
  }
}
