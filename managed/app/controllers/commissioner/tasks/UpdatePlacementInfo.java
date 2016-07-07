// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;


import com.fasterxml.jackson.databind.JsonNode;
import java.util.Collection;
import java.util.HashSet;
import java.util.Set;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

import controllers.commissioner.AbstractTaskBase;
import forms.commissioner.ITaskParams;
import forms.commissioner.TaskParamsBase;
import models.commissioner.InstanceInfo;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.master.Master;
import org.yb.WireProtocol;
import services.YBClientService;

public class UpdatePlacementInfo extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpdatePlacementInfo.class);

  // Parameters for placement info update task.
  public static class Params extends TaskParamsBase {
    public YBClientService ybService = null;
    // If true, then edit universe - will need to send new placement and blacklisted nodes.
    // If false, create universe case, just needs to set the new placement info.
    public boolean isEdit = false;
  }

  Params params;

  @Override
  public void initialize(ITaskParams params) {
    this.params = (Params)params;
  }

  public static Params getParams(boolean isEdit) {
    Params param = new UpdatePlacementInfo.Params();
    param.ybService = Play.current().injector().instanceOf(YBClientService.class);
    param.isEdit = isEdit;
    return param;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName() + "() : isEdit=" + params.isEdit;
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(params);
  }

  @Override
  public void run() {
    String hostPorts = TaskUtil.getMasterHostPorts(params.instanceUUID);
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);

      if (params.isEdit) {
        EditUniverseModifyConfig editConfig =
          new EditUniverseModifyConfig(params.ybService.getClient(hostPorts), params.instanceUUID);
        editConfig.doCall();
      } else {
        CreateUniverseModifyConfig createConfig =
          new CreateUniverseModifyConfig(params.ybService.getClient(hostPorts), params.instanceUUID);
        createConfig.doCall();
      }
    } catch (Exception e) {
      LOG.warn("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }
}
