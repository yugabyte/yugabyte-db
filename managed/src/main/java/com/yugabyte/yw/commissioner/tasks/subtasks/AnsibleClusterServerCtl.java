package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.DevOpsHelper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;

public class AnsibleClusterServerCtl extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleClusterServerCtl.class);

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
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
    String command = getDevOpsHelper().nodeCommand(DevOpsHelper.NodeCommandType.Control, taskParams());

    // Execute the ansible command.
    execCommand(command);
  }
}
