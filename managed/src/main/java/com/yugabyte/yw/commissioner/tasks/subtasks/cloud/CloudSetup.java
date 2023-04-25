/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.common.NetworkManager;
import javax.inject.Inject;
import play.libs.Json;

public class CloudSetup extends CloudTaskBase {

  private final NetworkManager networkManager;

  @Inject
  protected CloudSetup(BaseTaskDependencies baseTaskDependencies, NetworkManager networkManager) {
    super(baseTaskDependencies);
    this.networkManager = networkManager;
  }

  @Override
  protected CloudBootstrap.Params taskParams() {
    return (CloudBootstrap.Params) taskParams;
  }

  @Override
  public void run() {
    // TODO(bogdan): we do not actually do anything with this response, so can NOOP if not
    // creating any elements?
    JsonNode response =
        networkManager.bootstrap(
            null, taskParams().providerUUID, Json.stringify(Json.toJson(taskParams())));
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }
  }
}
