package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;

public class AnsibleClusterServerCtl extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleClusterServerCtl.class);

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
    public int sleepAfterCmdMills = 0;
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
    // Execute the ansible command.
    ShellProcessHandler.ShellResponse response = getNodeManager().nodeCommand(
        NodeManager.NodeCommandType.Control, taskParams());
    logShellResponse(response);

    if (taskParams().sleepAfterCmdMills > 0) {
      try {
        Thread.sleep(taskParams().sleepAfterCmdMills);
      } catch (InterruptedException e) {
        LOG.error("{} Thread Sleep failed: {}", getName(), e.getMessage());
      }
    }
  }
}
