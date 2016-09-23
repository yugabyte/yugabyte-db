package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.Common;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Universe;

public class AnsibleConfigureServers extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleConfigureServers.class);

  public static class Params extends NodeTaskParams {
    public boolean isMasterInShellMode = false;
    public String ybServerPkg;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String masterAddresses = Universe.get(taskParams().universeUUID).getMasterAddresses();
    String command = "ybcloud.py " + taskParams().cloud;


    if (taskParams().cloud == Common.CloudType.aws) {
      command += " --region " + taskParams().getRegion().code;
    }

    command += " instance configure" +
               " --package " + taskParams().ybServerPkg +
               " --master_addresses_for_tserver " + masterAddresses;

    if (!taskParams().isMasterInShellMode) {
      command += " --master_addresses_for_master " + masterAddresses;
    }

    command += " " + taskParams().nodeName;

    // Execute the ansible command.
    execCommand(command);
  }
}
