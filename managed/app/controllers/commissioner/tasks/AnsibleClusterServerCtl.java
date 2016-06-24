package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.Common;

public class AnsibleClusterServerCtl extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleClusterServerCtl.class);

  public static class Params extends AbstractTaskBase.TaskParamsBase {
    public String process;
    public String command;
  }

  public AnsibleClusterServerCtl(Params params) {
    super(params);
  }

  @Override
  public String getName() {
    Params params = (Params)taskParams;
    String classname = this.getClass().getSimpleName();
    return classname + "(" + taskParams.nodeInstanceName + "." + taskParams.cloud + ".yb, " +
           params.process + ", " + params.command + ")";
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Common.getDevopsHome();
    Params params = (Params)taskParams;
    String command = ybDevopsHome + "/bin/yb_cluster_server_ctl.sh" +
                     " --instance-name " + params.nodeInstanceName +
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
