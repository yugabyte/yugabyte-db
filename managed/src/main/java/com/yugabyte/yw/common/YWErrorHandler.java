/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import javax.inject.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.api.OptionalSourceMapper;
import play.api.routing.Router;
import play.http.DefaultHttpErrorHandler;
import play.mvc.Http;
import play.mvc.Http.RequestHeader;
import play.mvc.Result;

@Singleton
public final class YWErrorHandler extends DefaultHttpErrorHandler {
  public static final Logger LOG = LoggerFactory.getLogger(YWErrorHandler.class);

  @Inject
  public YWErrorHandler(
      Config config,
      Environment environment,
      OptionalSourceMapper sourceMapper,
      Provider<Router> routes) {
    super(config, environment, sourceMapper, routes);
    LOG.info("Created YWErrorHandler");
  }

  @Override
  public CompletionStage<Result> onServerError(Http.RequestHeader request, Throwable exception) {
    LOG.debug("YWErrorHandler invoked {}", exception.getMessage());
    for (Throwable cause : Throwables.getCausalChain(exception)) {
      if (cause instanceof PlatformServiceException) {
        return CompletableFuture.completedFuture(((PlatformServiceException) cause).getResult());
      }
    }
    return super.onServerError(request, exception);
  }

  @Override
  public CompletionStage<Result> onClientError(
      RequestHeader request, int statusCode, String message) {
    if (request.accepts("application/json")) {
      LOG.trace("Json formatting client error {}: {}", statusCode, message);
      return CompletableFuture.completedFuture(
          new PlatformServiceException(statusCode, message).getResult());
    }
    return super.onClientError(request, statusCode, message);
  }
}
