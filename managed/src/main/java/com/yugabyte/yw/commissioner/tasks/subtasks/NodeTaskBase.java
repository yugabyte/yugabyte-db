package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.common.DevOpsHelper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.UniverseDetails;

import play.api.Play;
import play.libs.Json;

public abstract class NodeTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(NodeTaskBase.class);

  // The task params.
  protected NodeTaskParams taskParams;

  private DevOpsHelper devOpsHelper;
  public DevOpsHelper getDevOpsHelper() { return devOpsHelper; }

  @Override
  protected NodeTaskParams taskParams() {
    return taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.devOpsHelper = Play.current().injector().instanceOf(DevOpsHelper.class);
    this.taskParams = (NodeTaskParams)params;
  }

  @Override
  public String getName() {
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
        UniverseDetails universeDetails = universe.getUniverseDetails();
        NodeDetails node = universeDetails.nodeDetailsMap.get(taskParams().nodeName);
        node.state = state;
        LOG.debug("Setting node {} state to {} in universe {}.", 
                  taskParams().nodeName, state, taskParams().universeUUID);
        // Update the node details.
        universeDetails.nodeDetailsMap.put(taskParams().nodeName, node);
        universe.setUniverseDetails(universeDetails);
      }
    };

    Universe.saveDetails(taskParams().universeUUID, updater);
  }
}
