// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.ITask;
import forms.commissioner.ITaskParams;

public class DestroyInstance extends InstanceTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(DestroyInstance.class);

  @Override
  public String getName() {
    return "DestroyInstance(???)";
  }

  @Override
  public JsonNode getTaskDetails() {
    return null;
  }

  @Override
  public void run() {
    throw new UnsupportedOperationException("Operation not implemented.");
  }

  @Override
  public int getPercentCompleted() {
    return 0;
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name : " + this.getClass().getName());
    return sb.toString();
  }
}
