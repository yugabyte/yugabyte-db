package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;

import play.libs.Json;

public abstract class NodeTaskBase extends AbstractTaskBase{
  public static final Logger LOG = LoggerFactory.getLogger(NodeTaskBase.class);

  // The task params.
  protected NodeTaskParams taskParams;

  @Override
  public void initialize(ITaskParams params) {
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
}
