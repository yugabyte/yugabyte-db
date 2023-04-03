/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.models.HighAvailabilityConfig;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

public class HAAuthenticator extends Action.Simple {
  public static final String HA_CLUSTER_KEY_TOKEN_HEADER = "HA-AUTH-TOKEN";

  private boolean clusterKeyValid(String clusterKey) {
    return HighAvailabilityConfig.get()
        .map(config -> config.getClusterKey().equals(clusterKey))
        .orElse(false);
  }

  @Override
  public CompletionStage<Result> call(Http.Request request) {
    return request
        .header(HA_CLUSTER_KEY_TOKEN_HEADER)
        .filter(this::clusterKeyValid)
        .map(success -> delegate.call(request))
        .orElse(
            CompletableFuture.completedFuture(
                Results.badRequest("Unable to authenticate request")));
  }
}
