package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.util.List;
import java.util.concurrent.CompletionStage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Action;
import play.mvc.Http.Context;
import play.mvc.Result;
import play.routing.Router;

public class AuditAction extends Action.Simple {

  public static final Logger LOG = LoggerFactory.getLogger(AuditAction.class);
  private final RuntimeConfigFactory runtimeConfigFactory;
  private static final String YB_AUDIT_LOG_VERIFY_LOGGING = "yb.audit.log.verifyLogging";

  @Inject
  public AuditAction(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  @Override
  public CompletionStage<Result> call(Context ctx) {

    if (!runtimeConfigFactory.globalRuntimeConf().getBoolean(YB_AUDIT_LOG_VERIFY_LOGGING)) {
      return delegate.call(ctx);
    } else {
      return delegate
          .call(ctx)
          .thenApply(
              result -> {
                if (result.status() == 200) {
                  boolean isAudited = (boolean) ctx.args.getOrDefault("isAudited", false);

                  // modifiers are to be added in v1.routes file
                  List<String> modifiers =
                      ctx.request().attrs().get(Router.Attrs.HANDLER_DEF).getModifiers();

                  boolean shouldBeAudited = !ctx.request().method().equals("GET");
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
                        " Mismatch in Audit Logging intent for " + ctx.request().path());
                  }
                }
                return result;
              });
    }
  }
}
