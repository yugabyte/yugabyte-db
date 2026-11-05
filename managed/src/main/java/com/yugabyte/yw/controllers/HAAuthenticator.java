/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;

public class HAAuthenticator extends Action.Simple {
  public static final String HA_CLUSTER_KEY_TOKEN_HEADER = "HA-AUTH-TOKEN";
  // The sender's yb.fips.enabled. Peers that predate it don't send it, and are not checked.
  public static final String HA_FIPS_ENABLED_HEADER = "HA-FIPS-ENABLED";
  // The requesting instance matches on this prefix to tell a FIPS mismatch from other errors.
  public static final String FIPS_MODE_MISMATCH_ERROR =
      "HA requires both YBA instances to have the same FIPS mode";

  private final boolean fipsEnabled;

  @Inject
  public HAAuthenticator(Config config) {
    this.fipsEnabled = config.getBoolean(CommonUtils.FIPS_ENABLED);
  }

  private boolean clusterKeyValid(String clusterKey) {
    return HighAvailabilityConfig.get()
        .map(config -> config.getClusterKey().equals(clusterKey))
        .orElse(false);
  }

  @Override
  public CompletionStage<Result> call(Http.Request request) {
    if (request.header(HA_CLUSTER_KEY_TOKEN_HEADER).filter(this::clusterKeyValid).isEmpty()) {
      return CompletableFuture.completedFuture(
          new PlatformServiceException(FORBIDDEN, "Unable to authenticate request")
              .buildResult(request));
    }
    Optional<Boolean> senderFipsEnabled =
        request.header(HA_FIPS_ENABLED_HEADER).map(Boolean::parseBoolean);
    if (senderFipsEnabled.isPresent() && senderFipsEnabled.get() != fipsEnabled) {
      String message =
          String.format(
              "%s: this instance is %s, the requesting instance is %s",
              FIPS_MODE_MISMATCH_ERROR, fipsModeName(fipsEnabled), fipsModeName(!fipsEnabled));
      return CompletableFuture.completedFuture(
          new PlatformServiceException(BAD_REQUEST, message).buildResult(request));
    }
    return delegate.call(request);
  }

  public static String fipsModeName(boolean fipsEnabled) {
    return fipsEnabled ? "FIPS-enabled" : "not FIPS-enabled";
  }
}
