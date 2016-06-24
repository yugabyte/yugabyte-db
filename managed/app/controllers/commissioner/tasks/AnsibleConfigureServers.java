package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.Common;
import models.commissioner.InstanceInfo;

public class AnsibleConfigureServers extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleConfigureServers.class);

  public static class Params extends AbstractTaskBase.TaskParamsBase {
    public String ybServerPkg;
  }

  public AnsibleConfigureServers(Params params) {
    super(params);
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Common.getDevopsHome();
    Params params = (Params)taskParams;
    String command = ybDevopsHome + "/bin/yb_cluster_server_configure.sh" +
                     " --instance-name " + params.nodeInstanceName +
                     " --package " + params.ybServerPkg +
                     " --master_addresses " + InstanceInfo.getMasterAddresses(params.instanceUUID);
    // Execute the ansible command.
    execCommand(command);
  }
}
