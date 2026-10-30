// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.SessionManagementHandler;
import api.v2.models.LoginSpec;
import api.v2.models.Session;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import play.mvc.Http;

public class SessionApiControllerImp extends SessionApiControllerImpInterface {
  private final SessionManagementHandler sessionHandler;

  @Inject
  public SessionApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      SessionManagementHandler sessionHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.sessionHandler = sessionHandler;
  }

  @Override
  public Session login(Http.Request request, LoginSpec loginSpec) {
    return sessionHandler.login(request, loginSpec);
  }
}
