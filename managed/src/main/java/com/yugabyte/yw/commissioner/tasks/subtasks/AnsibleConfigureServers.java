package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.util.HashMap;
import java.util.Map;

public class AnsibleConfigureServers extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleConfigureServers.class);

  public static class Params extends NodeTaskParams {
    public UpgradeUniverse.UpgradeTaskType type = UpgradeUniverse.UpgradeTaskType.Everything;
    public String ybSoftwareVersion;

    // Optional params
    public boolean isMasterInShellMode = false;
    public Map<String, String> gflags = new HashMap<>();
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    // Execute the ansible command.
    ShellProcessHandler.ShellResponse response = getNodeManager().nodeCommand(
        NodeManager.NodeCommandType.Configure, taskParams());
    logShellResponse(response);

    if (taskParams().type == UpgradeUniverse.UpgradeTaskType.Everything) {
      // We set the node state to SoftwareInstalled when configuration type is Everything
      setNodeState(NodeDetails.NodeState.SoftwareInstalled);
    }
  }
}
