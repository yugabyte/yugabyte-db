// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeConfigResponse;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.NodeDetails;

import play.api.Play;

public class ChangeMasterConfig extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ChangeMasterConfig.class);

  // The YB client to use.
  public YBClientService ybService;

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
  public void run() {
    try {
      // Get the master addresses.
      String masterAddresses = Universe.get(taskParams().universeUUID).getMasterAddresses();
      LOG.info("Running {}: universe = {}, masterAddress = {}", getName(),
               taskParams().universeUUID, masterAddresses);
      if (masterAddresses == null || masterAddresses.isEmpty()) {
        throw new IllegalStateException("No master host/ports for a change config op in " +
            taskParams().universeUUID);
      }

      // Get the node details.
      NodeDetails node = Universe.get(taskParams().universeUUID).getNode(taskParams().nodeName);
      // Perform the change config operation.
      boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
      ChangeConfigResponse response =
          ybService.getClient(masterAddresses).changeMasterConfig(node.private_ip,
                                                                  node.masterRpcPort,
                                                                  isAddMasterOp);
      if (response.hasError()) {
        LOG.warn("{} response has error {}.", getName(), response.errorMessage());
        throw new Exception("Change Config response has error " + response.errorMessage());
      }
    } catch (Exception e) {
      LOG.warn("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }
}
