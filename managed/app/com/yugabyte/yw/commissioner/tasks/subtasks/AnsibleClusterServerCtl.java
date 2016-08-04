package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;

public class AnsibleClusterServerCtl extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleClusterServerCtl.class);

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().nodeName + ", " +
           taskParams().process + ": " + taskParams().command + ")";
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String command = "yb_server_ctl.py " +  taskParams().nodeName +
                     " --cloud " + taskParams().cloud +
                     " --region " + taskParams().getRegion().code +
                     " " + taskParams().process +
                     " --command " + taskParams().command;
    // Execute the ansible command.
    execCommand(command);
  }
}
