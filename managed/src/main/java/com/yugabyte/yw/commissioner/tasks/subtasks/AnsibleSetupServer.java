// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;

import java.util.List;


public class AnsibleSetupServer extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The VPC into which the node is to be provisioned.
    public String subnetId;
    // Spot-price for universe (If aws and spot-price is desired, the value must be greater than 0).
    public double spotPrice = 0.0;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    Provider p = taskParams().getProvider();
    List<AccessKey> accessKeys = AccessKey.getAll(p.uuid);
    // Note: For now, we assume there will only be one AccessKey.
    // Also, should only execute Provision command if there is no externally accessible provision script.
    if (accessKeys.size() > 0 && accessKeys.get(0).getKeyInfo().provisionInstanceScript.length() == 0) {
      // Execute the ansible command.
      ShellProcessHandler.ShellResponse response = getNodeManager().nodeCommand(
          NodeManager.NodeCommandType.Provision, taskParams());
      logShellResponse(response);
    } else {
      LOG.info("Skipping ansible provision because provider " + p.code + " does not support passwordless sudo access.");
    }
  }
}
