// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.common.NetworkManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;


public class CloudSetup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudRegionSetup.class);

  public static class Params extends CloudBootstrap.Params {
    public String hostVpcRegion;
    public String hostVpcId;
    public String destVpcId;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    NetworkManager networkManager = Play.current().injector().instanceOf(NetworkManager.class);
    JsonNode response = networkManager.bootstrap(
        null,
        getProvider().uuid,
        taskParams().hostVpcRegion,
        taskParams().hostVpcId,
        taskParams().destVpcId);
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }
  }
}
