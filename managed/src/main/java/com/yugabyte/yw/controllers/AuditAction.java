package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.audit.AuditService.IS_AUDITED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.util.List;
import java.util.concurrent.CompletionStage;
import play.mvc.Action;
import play.mvc.Http.Request;
import play.mvc.Result;
import play.routing.Router;

public class AuditAction extends Action.Simple {
  private final RuntimeConfGetter confGetter;

  @Inject
  public AuditAction(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  @Override
  public CompletionStage<Result> call(Request request) {
    RequestContext.put(IS_AUDITED, false);
    try {
      if (!confGetter.getGlobalConf(GlobalConfKeys.auditVerifyLogging)) {
        return delegate.call(request);
      } else {
        return delegate
            .call(request)
            .thenApply(
                result -> {
                  if (result.status() == 200) {
                    boolean isAudited = RequestContext.getOrDefault(IS_AUDITED, false);

                    // modifiers are to be added in v1.routes file
                    List<String> modifiers =
                        request.attrs().get(Router.Attrs.HANDLER_DEF).getModifiers();

                    boolean shouldBeAudited = !request.method().equals("GET");
                    boolean hasForceNoAuditModifier = modifiers.contains("forceNoAudit");
                    boolean hasForceAuditModifier = modifiers.contains("forceAudit");

                    if (hasForceNoAuditModifier && hasForceAuditModifier) {
                      throw new PlatformServiceException(
                          INTERNAL_SERVER_ERROR,
                          " Method can't have both forceAudit and forceNoAudit.");
                    } else if (hasForceNoAuditModifier) {
                      shouldBeAudited = false;
                    } else if (hasForceAuditModifier) {
                      shouldBeAudited = true;
                    }

                    if (isAudited != shouldBeAudited) {
                      throw new PlatformServiceException(
                          INTERNAL_SERVER_ERROR,
                          " Mismatch in Audit Logging intent for "
                              + request.path()
                              + ": isAudited="
                              + isAudited
                              + ", but shouldBeAudited="
                              + shouldBeAudited);
                    }
                  }
                  return result;
                });
      }
    } finally {
      RequestContext.clean(ImmutableSet.of(IS_AUDITED));
    }
  }
}
