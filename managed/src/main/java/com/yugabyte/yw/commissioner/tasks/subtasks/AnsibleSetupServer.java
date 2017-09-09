// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;


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
    // Execute the ansible command.
    ShellProcessHandler.ShellResponse response = getNodeManager().nodeCommand(
        NodeManager.NodeCommandType.Provision, taskParams());
    logShellResponse(response);
  }
}
