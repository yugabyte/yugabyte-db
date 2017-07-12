package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;
import play.libs.Json;

public abstract class NodeTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(NodeTaskBase.class);

  private NodeManager nodeManager;
  public NodeManager getNodeManager() { return nodeManager; }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    this.nodeManager = Play.current().injector().instanceOf(NodeManager.class);
  }

  @Override
  public String getName() {
    NodeTaskParams taskParams = taskParams();
    return super.getName() + "(" + taskParams.universeUUID + ", " + taskParams.nodeName + ")";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  // Helper API to update the db for the current node with the given state.
  public void setNodeState(NodeDetails.NodeState state) {
    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        NodeDetails node = universe.getNode(taskParams().nodeName);
        node.state = state;
        LOG.debug("Setting node {} state to {} in universe {}.", 
                  taskParams().nodeName, state, taskParams().universeUUID);
        // Update the node details.
        universeDetails.nodeDetailsSet.add(node);
        universe.setUniverseDetails(universeDetails);
      }
    };

    Universe.saveDetails(taskParams().universeUUID, updater);
  }

  /**
   * Log the output of shellResponse to STDOUT or STDERR
   * @param response : ShellResponse object
   */
  public void logShellResponse(ShellProcessHandler.ShellResponse response) {
    if (response.code == 0) {
      LOG.info("[" + getName() + "] STDOUT: " + response.message);
    } else {
      throw new RuntimeException(response.message);
    }
  }
}
