// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public class ChangeMasterConfig extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ChangeMasterConfig.class);

  // The YB client to use.
  public YBClientService ybService;

  // Sleep time in each iteration while trying to check on master leader.
  public final long PER_ATTEMPT_SLEEP_TIME_MS = 100;

  // Create an enum specifying the operation type.
  public enum OpType {
    AddMaster,
    RemoveMaster
  }

  // Parameters for change master config task.
  public static class Params extends NodeTaskParams {
    // When true, the master hostPort is added to the current universe's quorum, otherwise it is
    // deleted.
    public OpType opType;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().nodeName +
           ", " + taskParams().opType.toString() + ")";
  }

  @Override
  public String toString() {
    return getName();
  }

  @Override
  public void run() {
    // Get the master addresses.
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    LOG.info("Running {}: universe = {}, masterAddress = {}", getName(),
             taskParams().universeUUID, masterAddresses);
    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalStateException("No master host/ports for a change config op in " +
          taskParams().universeUUID);
    }

    YBClient client = ybService.getClient(masterAddresses);
    // Get the node details and perform the change config operation.
    NodeDetails node = universe.getNode(taskParams().nodeName);
    boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
    LOG.info("Starting changeMasterConfig({}:{}, {})",
             node.cloudInfo.private_ip, node.masterRpcPort, taskParams().opType);
    ChangeConfigResponse response;
    try {
      response = client.changeMasterConfig(
          node.cloudInfo.private_ip, node.masterRpcPort, isAddMasterOp);
    } catch (Exception e) {
      String msg = "Error performing change config on node " + node.nodeName +
                   ", host:port = " + node.cloudInfo.private_ip + ":" + node.masterRpcPort;
      LOG.error(msg, e);
      throw new RuntimeException(msg);
    }
    // If there was an error, throw an exception.
    if (response.hasError()) {
      String msg = "ChangeConfig response has error " + response.errorMessage();
      LOG.error(msg);
      throw new RuntimeException(msg);
    }
  }
}
