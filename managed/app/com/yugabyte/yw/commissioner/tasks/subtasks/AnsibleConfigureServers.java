package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;

public class AnsibleConfigureServers extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleConfigureServers.class);

  public static class Params extends NodeTaskParams {
    public String ybServerPkg;
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Util.getDevopsHome();
    Params params = (Params)taskParams;
    String command = ybDevopsHome + "/bin/yb_cluster_server_configure.sh" +
                     " --instance-name " + params.nodeName +
                     " --package " + params.ybServerPkg +
                     " --master_addresses " +
                     Universe.get(params.universeUUID).getMasterAddresses();
    // Execute the ansible command.
    execCommand(command);
  }
}
