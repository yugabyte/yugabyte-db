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

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;

@Slf4j
public class ChangeMasterConfig extends AbstractTaskBase {

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
    // Use hostport to remove a master from quorum, when this is set. No errors are rethrown
    // as this is best effort.
    public boolean useHostPort = false;
    // Check if the operation is already performed before it is done again.
    // This is done to have no impact on the existing usage.
    public boolean checkBeforeChange = false;
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
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalStateException(
          "No master host/ports for a change config op in " + taskParams().universeUUID);
    }
    String certificate = universe.getCertificateNodetoNode();

    // Get the node details and perform the change config operation.
    NodeDetails node = universe.getNode(taskParams().nodeName);
    boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
    log.info(
        "Starting changeMasterConfig({}:{}, op={}, useHost={})",
        node.cloudInfo.private_ip,
        node.masterRpcPort,
        taskParams().opType.toString(),
        taskParams().useHostPort);
    ChangeConfigResponse response = null;
    YBClientService.Config config = new YBClientService.Config(masterAddresses, certificate);
    config.setAdminOperationTimeout(YBCLIENT_ADMIN_OPERATION_TIMEOUT);
    YBClient client = ybService.getClientWithConfig(config);
    try {
      // The call changeMasterConfig is not idempotent. The client library internally keeps retrying
      // for a long time until it gives up if the node is already added or removed.
      // This optional check ensures that changeMasterConfig is not invoked if the operation is
      // already done for the node.
      if (taskParams().checkBeforeChange && isChangeMasterConfigDone(client, node, isAddMasterOp)) {
        log.info(
            "Config change (add={}) is already done for node {}({}:{})",
            isAddMasterOp,
            node.nodeName,
            node.cloudInfo.private_ip,
            node.masterRpcPort);
        return;
      }
      response =
          client.changeMasterConfig(
              node.cloudInfo.private_ip,
              node.masterRpcPort,
              isAddMasterOp,
              taskParams().useHostPort);
    } catch (Exception e) {
      String msg =
          "Error "
              + e.getMessage()
              + " while performing change config on node "
              + node.nodeName
              + ", host:port = "
              + node.cloudInfo.private_ip
              + ":"
              + node.masterRpcPort;
      log.error(msg, e);
      if (!taskParams().useHostPort) {
        throw new RuntimeException(msg);
      } else {
        // Do not throw error as host/port is only to be used when caller knows that this peer is
        // already dead.
        response = null;
      }
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
    // If there was an error, throw an exception.
    if (response != null && response.hasError()) {
      String msg = "ChangeConfig response has error " + response.errorMessage();
      log.error(msg);
      throw new RuntimeException(msg);
    }
  }

  private boolean isChangeMasterConfigDone(
      YBClient client, NodeDetails node, boolean isAddMasterOp) {
    try {
      ListMastersResponse response = client.listMasters();
      List<ServerInfo> servers = response.getMasters();
      boolean anyMatched =
          servers.stream().anyMatch(s -> s.getHost().equals(node.cloudInfo.private_ip));
      return anyMatched == isAddMasterOp;
    } catch (Exception e) {
      String msg =
          "Error "
              + e.getMessage()
              + " while performing list masters for node "
              + node.nodeName
              + ", host:port = "
              + node.cloudInfo.private_ip
              + ":"
              + node.masterRpcPort;
      log.error(msg, e);
      throw new RuntimeException(msg);
    }
  }
}
