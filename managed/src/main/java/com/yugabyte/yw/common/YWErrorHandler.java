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

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;

import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import javax.inject.Provider;
import javax.persistence.OptimisticLockException;
import org.jetbrains.annotations.NotNull;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.api.OptionalSourceMapper;
import play.api.UsefulException;
import play.api.routing.Router;
import play.http.DefaultHttpErrorHandler;
import play.libs.Json;
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
        return CompletableFuture.completedFuture(
            ((PlatformServiceException) cause).buildResult(request.method(), request.uri()));
      }
      if (cause instanceof OptimisticLockException) {
        return CompletableFuture.completedFuture(
            new PlatformServiceException(
                    BAD_REQUEST, "Data has changed, please refresh and try again")
                .buildResult(request.method(), request.uri()));
      }
    }
    return super.onServerError(request, exception);
  }

  @Override
  protected CompletionStage<Result> onProdServerError(
      RequestHeader request, UsefulException exception) {
    if (request.accepts("application/json")) {
      return CompletableFuture.completedFuture(
          new PlatformServiceException(
                  INTERNAL_SERVER_ERROR,
                  "HTTP Server Error. This exception has been logged with id " + exception)
              .buildResult(request));
    }
    return super.onProdServerError(request, exception);
  }

  @Override
  protected CompletionStage<Result> onDevServerError(
      RequestHeader request, UsefulException exception) {
    if (request.accepts("application/json")) {
      return CompletableFuture.completedFuture(
          new PlatformServiceException(
                  INTERNAL_SERVER_ERROR, "HTTP Server Error", Json.toJson(exception))
              .buildResult(request));
    }
    return super.onProdServerError(request, exception);
  }

  @NotNull
  public CompletableFuture<Result> onJsonClientError(
      RequestHeader request, int statusCode, String message) {
    LOG.trace("Json formatting client error {}: {}", statusCode, message);
    return CompletableFuture.completedFuture(
        new PlatformServiceException(statusCode, "HTTP Client Error: " + message)
            .buildResult(request));
  }

  @Override
  protected CompletionStage<Result> onBadRequest(RequestHeader request, String message) {
    if (request.accepts("application/json")) {
      // keep it same since we will have too many tests depending on this behaviour
      return CompletableFuture.completedFuture(
          new PlatformServiceException(BAD_REQUEST, message).buildResult(request));
    }
    return super.onBadRequest(request, message);
  }

  @Override
  protected CompletionStage<Result> onForbidden(RequestHeader request, String message) {
    if (request.accepts("application/json")) {
      return onJsonClientError(
          request, FORBIDDEN, String.format("(%d)Forbidden, details: '%s'", FORBIDDEN, message));
    }
    return super.onForbidden(request, message);
  }

  @Override
  protected CompletionStage<Result> onNotFound(RequestHeader request, String message) {
    if (request.accepts("application/json")) {
      return onJsonClientError(
          request, NOT_FOUND, String.format("%d(Not Found), details: %s", NOT_FOUND, message));
    }
    return super.onNotFound(request, message);
  }

  @Override
  protected CompletionStage<Result> onOtherClientError(
      RequestHeader request, int statusCode, String message) {
    if (request.accepts("application/json")) {
      return onJsonClientError(
          request,
          statusCode,
          String.format("Http Client Error Code: %d details: %s", statusCode, message));
    }
    return super.onOtherClientError(request, statusCode, message);
  }
}
