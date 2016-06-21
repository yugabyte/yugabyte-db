// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.Common.CloudType;
import forms.commissioner.TaskParamsBase;
import util.Util;

public class AnsibleSetupServer extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Additional parameters for this task.
  public static class Params extends TaskParamsBase {
    // The VPC into which the node is to be provisioned.
    public String vpcId;
  }

  public Params getParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    String ybDevopsHome = Util.getDevopsHome();
    String command = ybDevopsHome + "/bin/setup_server.sh" +
                     " --cloud " + getParams().cloud +
                     " --instance-name " + getParams().nodeInstanceName +
                     " --type test-cluster-server";

    // Add the appropriate VPC ID parameter if this is an AWS deployment.
    if (getParams().cloud == CloudType.aws) {
      command += " --extra-vars aws_vpc_subnet_id=" + getParams().vpcId;
    }
    // Execute the ansible command.
    execCommand(command);
  }
}
