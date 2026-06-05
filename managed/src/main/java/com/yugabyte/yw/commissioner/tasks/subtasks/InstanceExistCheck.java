// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstanceExistCheck extends NodeTaskBase {

  @Inject
  protected InstanceExistCheck(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    if (instanceExists(taskParams())) {
      log.info("Waiting for connection to succeed on existing instance {}", taskParams().nodeName);
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Wait_For_Connection, taskParams())
          .processErrors();
    }
  }

  @Override
  public boolean onFailure(TaskInfo taskInfo, Throwable cause) {
    Universe universe = getUniverse();
    NodeDetails node = universe.getNode(taskParams().nodeName);

    if (Util.getProviderForNode(node, universe).getCloudCode() == Common.CloudType.onprem) {
      return false;
    }

    return super.onFailure(taskInfo, cause);
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
