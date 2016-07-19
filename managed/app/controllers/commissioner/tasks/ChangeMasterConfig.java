// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeConfigResponse;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.tasks.ChangeMasterConfig.OpType;
import forms.commissioner.ITaskParams;
import forms.commissioner.UniverseTaskParams;
import models.commissioner.Universe;
import models.commissioner.Universe.NodeDetails;
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
  public static class Params extends UniverseTaskParams {
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
    return "ChangeMasterConfig(" + taskParams.nodeName + ", " +
           taskParams.opType.toString() + ")";
  }

  @Override
  public void run() {
    try {
      // Get the master addresses.
      String masterAddresses = Universe.get(taskParams.universeUUID).getMasterAddresses();
      LOG.info("Running {}: universe = {}, masterAddress = {}", getName(),
               taskParams.universeUUID, masterAddresses);
      if (masterAddresses == null || masterAddresses.isEmpty()) {
        throw new IllegalStateException("No master host/ports for a change config op in " +
            taskParams.universeUUID);
      }

      // Get the node details.
      NodeDetails node = Universe.get(taskParams.universeUUID).getNode(taskParams.nodeName);
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
