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

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.YBClient;

@Slf4j
public class ChangeMasterConfig extends UniverseTaskBase {

  private static final Duration YBCLIENT_ADMIN_OPERATION_TIMEOUT = Duration.ofMinutes(15);

  @Inject
  protected ChangeMasterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    log.info(
        "Running {}: universe = {}, masterAddress = {}",
        getName(),
        taskParams().universeUUID,
        masterAddresses);
    if (StringUtils.isBlank(masterAddresses)) {
      throw new IllegalStateException(
          "No master host/ports for a change config op in " + taskParams().universeUUID);
    }

    // Get the node details and perform the change config operation.
    NodeDetails node = universe.getNode(taskParams().nodeName);
    boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
    // If the cluster has a secondary IP, we want to ensure that we use the correct addresses.
    // The ipToUse is the address that we need to add to the config.
    // The ipForPlatform is the address that the platform uses to connect to the host.
    boolean hasSecondaryIp =
        node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    boolean shouldUseSecondary =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("false");
    String ipToUse =
        hasSecondaryIp && shouldUseSecondary
            ? node.cloudInfo.secondary_private_ip
            : node.cloudInfo.private_ip;
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
    String certificate = universe.getCertificateNodetoNode();
    YBClientService.Config config = new YBClientService.Config(masterAddresses, certificate);
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
  }
}
