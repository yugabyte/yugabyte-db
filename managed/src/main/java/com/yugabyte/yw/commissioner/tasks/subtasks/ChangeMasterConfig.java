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
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.YBClient;

import javax.inject.Inject;

@Slf4j
public class ChangeMasterConfig extends AbstractTaskBase {
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
    YBClient client;
    client = ybService.getClient(masterAddresses, certificate);

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
    try {
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
}
