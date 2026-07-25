// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.DrConfigHandler;
import api.v2.models.DrConfigDbDetailPagedQuerySpec;
import api.v2.models.DrConfigDbDetailPagedResp;
import api.v2.models.DrConfigPagedQuerySpec;
import api.v2.models.DrConfigPagedResp;
import api.v2.models.DrConfigTableDetailPagedQuerySpec;
import api.v2.models.DrConfigTableDetailPagedResp;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http.Request;

public class DisasterRecoveryApiControllerImp extends DisasterRecoveryApiControllerImpInterface {

  private final DrConfigHandler handler;

  @Inject
  public DisasterRecoveryApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      DrConfigHandler handler) {
    super(auditService, config, gFlagsAuditHandler);
    this.handler = handler;
  }

  @Override
  public DrConfigPagedResp pageListDrConfigs(
      Request request, UUID cUUID, UUID uniUUID, DrConfigPagedQuerySpec drConfigPagedQuerySpec)
      throws Exception {
    return handler.pageListDrConfigs(cUUID, uniUUID, drConfigPagedQuerySpec);
  }

  @Override
  public DrConfigTableDetailPagedResp pageListDrConfigTables(
      Request request,
      UUID cUUID,
      UUID drUUID,
      DrConfigTableDetailPagedQuerySpec drConfigTableDetailPagedQuerySpec)
      throws Exception {
    return handler.pageListDrConfigTables(cUUID, drUUID, drConfigTableDetailPagedQuerySpec);
  }

  @Override
  public DrConfigDbDetailPagedResp pageListDrConfigDatabases(
      Request request,
      UUID cUUID,
      UUID drUUID,
      DrConfigDbDetailPagedQuerySpec drConfigDbDetailPagedQuerySpec)
      throws Exception {
    return handler.pageListDrConfigDatabases(cUUID, drUUID, drConfigDbDetailPagedQuerySpec);
  }
}
