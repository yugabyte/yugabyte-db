// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;


import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DevOpsHelper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;

public class AnsibleUpgradeServer extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleUpgradeServer.class);

  public static class Params extends NodeTaskParams {
    public UpgradeUniverse.UpgradeTaskType taskType;
    public String ybServerPkg;
    public Map<String, String> gflags;
  }

  @Override
  protected AnsibleUpgradeServer.Params taskParams() {
    return (AnsibleUpgradeServer.Params)taskParams;
  }

  @Override
  public void run() {
    // TODO: add begin and end state

    String command = getDevOpsHelper().nodeCommand(DevOpsHelper.NodeCommandType.Configure, taskParams());

    // Execute the ansible command.
    execCommand(command);
  }
}
