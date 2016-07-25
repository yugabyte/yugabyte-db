package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;

public class AnsibleClusterServerCtl extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleClusterServerCtl.class);

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
  }

  Params taskParams;

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = (Params)params;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams.nodeName + ", " +
           taskParams.process + ": " + taskParams.command + ")";
  }

  @Override
  public void run() {
    Params params = taskParams;
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Util.getDevopsHome();
    String command = ybDevopsHome + "/bin/yb_cluster_server_ctl.sh" +
                     " --instance-name " + params.nodeName +
                     " --process " + params.process +
                     " --command " + params.command;
    // Execute the ansible command.
    execCommand(command);

    // TODO: make sure the process command has completed. Especially make sure the cluster has been
    // created when process == master and command == create.
    try {
      Thread.sleep(5000);
    } catch (InterruptedException e) {
      LOG.error("Error waiting for " + command + "to complete.", e);
    }
  }
}
