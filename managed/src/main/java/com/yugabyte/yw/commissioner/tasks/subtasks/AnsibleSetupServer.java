// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;

public class AnsibleSetupServer extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The VPC into which the node is to be provisioned.
    public String subnetId;

    // The instance type that needs to be provisioned.
    public String instanceType;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    String command = "ybcloud.py " +  taskParams().cloud +
                     " --region " + taskParams().getRegion().code +
                     " instance provision";

    command += " --cloud_subnet " + taskParams().subnetId;
    command += " --machine_image " + taskParams().getRegion().ybImage;
    command += " --instance_type " + taskParams().instanceType;
    command += " --assign_public_ip";

    command += " --reuse_host " + taskParams().nodeName;

    // Execute the ansible command.
    execCommand(command);
  }
}
