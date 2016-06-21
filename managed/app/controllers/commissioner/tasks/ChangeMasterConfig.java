// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.ITask;
import forms.commissioner.InstanceTaskParams;
import forms.commissioner.ITaskParams;
import models.commissioner.InstanceInfo;

import org.yb.client.ChangeConfigResponse;
import services.YBClientService;
import util.Util;

public class ChangeMasterConfig extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ChangeMasterConfig.class);

  // Parameters for change master config task.
  public static class Params extends InstanceTaskParams {
    public YBClientService ybService;
    public HostAndPort hostPort;
    // When true, the master hostPort is added to the current universe's quroum, otherwise it is deleted.
    public boolean isAdd;
  }

  Params taskParams;

  @Override
  public void initialize(ITaskParams params) {
    taskParams = (Params)params;
  }

  public static Params getParams(HostAndPort hostPort, boolean isAdd) {
    Params taskParam = new ChangeMasterConfig.Params();
    taskParam.ybService = Play.current().injector().instanceOf(YBClientService.class);
    taskParam.hostPort = hostPort;
    taskParam.isAdd = isAdd;
    return taskParam;
  }

  @Override
  public String getName() {
    return "ChangeMasterConfig(master=" + taskParams.hostPort + ", add=" + taskParams.isAdd + ")";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public String toString() {
    return getName() + ": details=" + getTaskDetails();
  }

  @Override
  public void run() {
    try {
      String hostPorts = TaskUtil.getMasterHostPorts(taskParams.instanceUUID);

      LOG.info("Running Change Config isAdd={} uuid={} ports={}", taskParams.isAdd,
               taskParams.instanceUUID, hostPorts);

      if (hostPorts == null || hostPorts.isEmpty()) {
        throw new IllegalStateException("No master host/ports for a change config op in " +
            taskParams.instanceUUID);
      }

      // If node is being added, find the new ones from the db and get one of their host/port.
      if (taskParams.isAdd) {
        InstanceInfo.NodeDetails addNode =
            InstanceInfo.findNewNodeAndUpdateIt(taskParams.instanceUUID);

        if (addNode != null) {
          taskParams.hostPort = HostAndPort.fromParts(addNode.public_ip, addNode.masterRpcPort);
          LOG.info("Found {} to add", taskParams.hostPort.toString());
        } else {
          throw new IllegalStateException("Could not find a new node for a change config op in " +
                                          taskParams.instanceUUID);
        }
      }

      ChangeConfigResponse response = taskParams.ybService
          .getClient(hostPorts)
          .ChangeMasterConfig(
               taskParams.hostPort.getHostText(),
               taskParams.hostPort.getPort(),
               taskParams.isAdd);
      if (response.hasError()) {
        LOG.warn("{} response has error {}.", getName(), response.errorMessage());
        throw new Exception("Change Config response has error " + response.errorMessage());
      }
    } catch (Exception e) {
      LOG.warn("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }
}
