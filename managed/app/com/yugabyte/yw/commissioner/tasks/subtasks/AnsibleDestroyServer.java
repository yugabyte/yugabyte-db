// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;

public class AnsibleDestroyServer extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Params for this task.
  public static class Params extends NodeTaskParams {}

  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    String command = "yb_server_provision.py " + taskParams().nodeName +
                     " -c " + taskParams().cloud + " --destroy";

    // Execute the ansible command.
    execCommand(command);
  }
}
