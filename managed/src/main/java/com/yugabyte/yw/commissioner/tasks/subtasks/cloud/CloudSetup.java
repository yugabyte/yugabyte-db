// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.NetworkManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;


public class CloudSetup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudRegionSetup.class);

  public static class Params extends CloudTaskParams {
    public String customPayload;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    NetworkManager networkManager = Play.current().injector().instanceOf(NetworkManager.class);
    // TODO(bogdan): we do not actually do anything with this response, so can NOOP if not
    // creating any elements?
    JsonNode response = networkManager.bootstrap(
        null,
        taskParams().providerUUID,
        taskParams().customPayload);
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }
  }
}
