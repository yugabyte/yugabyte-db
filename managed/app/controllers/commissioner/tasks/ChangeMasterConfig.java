// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeConfigResponse;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.tasks.ChangeMasterConfig.OpType;
import forms.commissioner.ITaskParams;
import forms.commissioner.InstanceTaskParams;
import models.commissioner.InstanceInfo;
import models.commissioner.InstanceInfo.NodeDetails;
import play.api.Play;
import services.YBClientService;

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
  public static class Params extends InstanceTaskParams {
    // When true, the master hostPort is added to the current universe's quorum, otherwise it is
    // deleted.
    public OpType opType;
  }

  Params taskParams;

  @Override
  public void initialize(ITaskParams params) {
    taskParams = (Params)params;
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return "ChangeMasterConfig(" + taskParams.nodeInstanceName + ", " +
           taskParams.opType.toString() + ")";
  }

  @Override
  public void run() {
    try {
      // Get the master addresses.
      String masterAddresses = InstanceInfo.get(taskParams.instanceUUID).getMasterAddresses();
      LOG.info("Running {}: instance = {}, masterAddress = {}", getName(),
               taskParams.instanceUUID, masterAddresses);
      if (masterAddresses == null || masterAddresses.isEmpty()) {
        throw new IllegalStateException("No master host/ports for a change config op in " +
            taskParams.instanceUUID);
      }

      // Get the node details.
      NodeDetails node = InstanceInfo.get(taskParams.instanceUUID).getNode(taskParams.instanceName);
      // Perform the change config operation.
      boolean isAddMasterOp = (taskParams.opType == OpType.AddMaster);
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
