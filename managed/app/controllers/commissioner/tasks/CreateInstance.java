// Copyright (c) Yugabyte, Inc.

package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.ITask;
import forms.commissioner.CreateInstanceTaskParams;
import forms.commissioner.ITaskParams;
import play.libs.Json;

public class CreateInstance implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(CreateInstance.class);

  // The task params.
  CreateInstanceTaskParams taskParams;

  @Override
  public void initialize(ITaskParams taskParams) {
    this.taskParams = (CreateInstanceTaskParams)taskParams;
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public void run() throws UnsupportedOperationException {
    LOG.info("Started task.");
    try {
      Thread.sleep(1000);
    } catch (InterruptedException e) {
      // TODO Auto-generated catch block
      e.printStackTrace();
    }
    LOG.info("Finished task.");
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name : " + this.getClass().getName());
    return sb.toString();
  }
}
