// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;


import com.fasterxml.jackson.databind.JsonNode;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import controllers.commissioner.AbstractTaskBase;
import forms.commissioner.ITaskParams;
import forms.commissioner.TaskParamsBase;
import models.commissioner.InstanceInfo;

public class UpdateInstanceInfo extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpdateInstanceInfo.class);

  // Parameters for instance info update task.
  public static class Params extends TaskParamsBase {
    public boolean isSuccess = false;
  }

  @Override
  public String getName() {
    return "UpdateInstanceInfo()";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public void run() {
    Params params = (Params)taskParams;
    try {
      LOG.info("Running {} in success={} mode.", getName(), params.isSuccess);

      if (params.isSuccess) {
        InstanceInfo.switchEditToLatest(params.instanceUUID);
      } else {
        InstanceInfo.deleteEdit(params.instanceUUID);
      }
    } catch (Exception e) {
      LOG.warn("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name : " + getName() + " " + getTaskDetails().toString());
    return sb.toString();
  }
}
