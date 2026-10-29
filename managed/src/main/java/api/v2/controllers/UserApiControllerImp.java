// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.UserManagementHandler;
import api.v2.models.UserCreateSpec;
import api.v2.models.UserCreation;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class UserApiControllerImp extends UserApiControllerImpInterface {
  private final UserManagementHandler userHandler;

  @Inject
  public UserApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      UserManagementHandler userHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.userHandler = userHandler;
  }

  @Override
  public UserCreation createUser(
      Http.Request request, UUID cUUID, UserCreateSpec userCreateSpec) {
    return userHandler.createUser(cUUID, userCreateSpec);
  }
}
