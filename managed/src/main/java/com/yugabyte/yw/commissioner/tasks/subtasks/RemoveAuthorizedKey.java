package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeAccessTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RemoveAuthorizedKey extends NodeTaskBase {

  @Inject
  protected RemoveAuthorizedKey(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeAccessTaskParams taskParams() {
    return (NodeAccessTaskParams) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    UUID universeUUID = taskParams().getUniverseUUID();
    String nodeName = taskParams().nodeName;
    Universe universe = Universe.getOrBadRequest(universeUUID);
    NodeDetails node = universe.getNodeOrBadRequest(nodeName);
    if (node.state != NodeDetails.NodeState.Live) {
      throw new RuntimeException("Node is in state" + node.state + " instead of LIVE");
    }
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Remove_Authorized_Key, taskParams())
        .processErrors();
  }
}
