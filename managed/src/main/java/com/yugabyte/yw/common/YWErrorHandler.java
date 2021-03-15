/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.api.OptionalSourceMapper;
import play.api.routing.Router;
import play.http.DefaultHttpErrorHandler;
import play.mvc.Http;
import play.mvc.Result;

import javax.inject.Provider;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

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
    LOG.debug("YWErrorHandler invoked {} ", exception.getMessage());
    for (Throwable cause : Throwables.getCausalChain(exception)) {
      if (cause instanceof YWServiceException) {
        return CompletableFuture.completedFuture(((YWServiceException) cause).getResult());
      }
    }
    return super.onServerError(request, exception);
  }
}
