package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Action;
import play.mvc.Http.Request;
import play.mvc.Result;

@Slf4j
public class FailedRequestAction extends Action.Simple {

  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public FailedRequestAction(RuntimeConfGetter runtimeConfGetter) {
    this.runtimeConfGetter = runtimeConfGetter;
  }

  @Override
  public CompletionStage<Result> call(Request request) {
    if (!runtimeConfGetter.getGlobalConf(
        GlobalConfKeys.enableTaskAndFailedRequestDetailedLogging)) {
      return delegate.call(request);
    } else {
      try {
        return delegate
            .call(request)
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
