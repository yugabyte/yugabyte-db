// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.YbaInstanceHandler;
import api.v2.models.YBAInfo;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import play.mvc.Http;

public class YbaInstanceApiControllerImp extends YbaInstanceApiControllerImpInterface {
  private final YbaInstanceHandler ybaInstanceHandler;

  @Inject
  public YbaInstanceApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      YbaInstanceHandler ybaInstanceHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.ybaInstanceHandler = ybaInstanceHandler;
  }

  @Override
  public YBAInfo getYBAInstanceInfo(Http.Request request) {
    return ybaInstanceHandler.getYBAInstanceInfo();
  }
}
