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
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Util.getDevopsHome();
    String command = ybDevopsHome + "/ybops/ybops/scripts/yb_server_configure.py" +
                     " --package " + taskParams().ybServerPkg +
                     " --cloud " + taskParams().cloud +
                     " --master_addresses " +
                     Universe.get(taskParams().universeUUID).getMasterAddresses() +
                     " " + taskParams().nodeName;
    // Execute the ansible command.
    execCommand(command);
  }
}
