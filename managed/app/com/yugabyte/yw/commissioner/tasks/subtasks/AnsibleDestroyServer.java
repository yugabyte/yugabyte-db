// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;

public class AnsibleDestroyServer extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Params for this task.
  public static class Params extends NodeTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    String aws_vpc_subnet_id = AvailabilityZone.find.byId(taskParams().azUuid).subnet;
    String command = "yb_server_provision.py " + taskParams().nodeName +
                     " --cloud " + taskParams().cloud +
                     " --region " + taskParams().getRegion().code +
                     " --aws_vpc_subnet_id " + aws_vpc_subnet_id +
                     " --destroy";

    // Execute the ansible command.
    execCommand(command);
  }
}
