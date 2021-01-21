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

import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public class HAAuthenticator extends Action.Simple {
  public static final String HA_CLUSTER_KEY_TOKEN_HEADER = "HA-AUTH-TOKEN";

  @Override
  public CompletionStage<Result> call(Http.Context ctx) {
    final Optional<String> requestClusterKey = ctx.request().header(HA_CLUSTER_KEY_TOKEN_HEADER);

    if (!requestClusterKey.isPresent()) {
      return CompletableFuture.completedFuture(
        Results.badRequest("Unable to authenticate request")
      );
    }

    final boolean isClusterKeyValid = HighAvailabilityConfig.list()
      .stream()
      .map(HighAvailabilityConfig::getClusterKey)
      .anyMatch(k -> k.equals(requestClusterKey.get()));

    if (!isClusterKeyValid) {
      return CompletableFuture.completedFuture(
        Results.badRequest("Unable to authenticate provided cluster key")
      );
    }

    return delegate.call(ctx);
  }
}
