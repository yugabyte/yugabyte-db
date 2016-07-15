package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import forms.commissioner.TaskParamsBase;
import models.commissioner.InstanceInfo;
import util.Util;

public class AnsibleConfigureServers extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleConfigureServers.class);

  public static class Params extends TaskParamsBase {
    public String ybServerPkg;
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Util.getDevopsHome();
    Params params = (Params)taskParams;
    String command = ybDevopsHome + "/bin/yb_cluster_server_configure.sh" +
                     " --instance-name " + params.nodeInstanceName +
                     " --package " + params.ybServerPkg +
                     " --master_addresses " +
                     InstanceInfo.get(params.instanceUUID).getMasterAddresses();
    // Execute the ansible command.
    execCommand(command);
  }
}
