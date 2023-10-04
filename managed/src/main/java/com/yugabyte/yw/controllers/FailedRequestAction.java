// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
public class FailedRequestAction extends Action.Simple {

  private final RuntimeConfigFactory runtimeConfigFactory;
  public static final String ENABLE_DETAILED_LOGGING_FAILED_REQUEST =
      "yb.logging.enable_task_failed_request_logs";

  @Inject
  public FailedRequestAction(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public CompletionStage<Result> call(Http.Context context) {
    Http.Request request = context.request();
    if (!runtimeConfigFactory
        .globalRuntimeConf()
        .getBoolean(ENABLE_DETAILED_LOGGING_FAILED_REQUEST)) {
      return delegate.call(context);
    } else {
      try {
        return delegate
            .call(context)
            .thenApply(
                result -> {
                  if (result.status() >= 400 && request.hasBody()) {
                    // In case of request failure log the request body.
                    JsonNode redactedBodyJson =
                        RedactingService.filterSecretFields(
                            request.body().asJson(), RedactionTarget.LOGS);
                    log.debug(
                        "{} request to endpoint {} with body {} failed.",
                        request.method(),
                        request.uri(),
                        redactedBodyJson);
                  }
                  return result;
                });
      } catch (Exception e) {
        if (request.hasBody()) {
          // In case of request failure log the request body.
          JsonNode redactedBodyJson =
              RedactingService.filterSecretFields(request.body().asJson(), RedactionTarget.LOGS);
          log.debug(
              "{} request to endpoint {} with body {} failed with exception {}",
              request.method(),
              request.uri(),
              redactedBodyJson,
              e.getMessage());
        }
        throw e;
      }
    }
  }
}
