package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.DevOpsHelper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;

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
    String command = getDevOpsHelper().nodeCommand(DevOpsHelper.NodeCommandType.Configure, taskParams());
    LOG.info("Command to run: [{}]", command);

    // Execute the ansible command.
    execCommand(command);

    // Update the node state once the software is installed.
    setNodeState(NodeDetails.NodeState.SoftwareInstalled);
  }
}
