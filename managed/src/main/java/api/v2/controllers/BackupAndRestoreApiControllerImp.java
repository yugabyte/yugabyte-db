// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.BackupAndRestoreHandler;
import api.v2.models.BackupPagedQuerySpec;
import api.v2.models.BackupPagedResp;
import api.v2.models.GflagMetadata;
import api.v2.models.RestoreKeyspacePagedQuerySpec;
import api.v2.models.RestoreKeyspacePagedResp;
import api.v2.models.RestorePagedQuerySpec;
import api.v2.models.RestorePagedResp;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.List;
import java.util.UUID;
import play.mvc.Http;

public class BackupAndRestoreApiControllerImp extends BackupAndRestoreApiControllerImpInterface {

  private final BackupAndRestoreHandler handler;

  @Inject
  public BackupAndRestoreApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      BackupAndRestoreHandler handler) {
    super(auditService, config, gFlagsAuditHandler);
    this.handler = handler;
  }

  @Override
  public List<GflagMetadata> listYbcGflagsMetadata(Http.Request request) throws Exception {
    return handler.listYbcGflagsMetadata();
  }

  @Override
  public BackupPagedResp pageListBackups(
      Http.Request request, UUID cUUID, BackupPagedQuerySpec backupPagedQuerySpec)
      throws Exception {
    return handler.pageListBackups(cUUID, backupPagedQuerySpec);
  }

  @Override
  public RestorePagedResp pageListRestores(
      Http.Request request, UUID cUUID, RestorePagedQuerySpec restorePagedQuerySpec)
      throws Exception {
    return handler.pageListRestores(cUUID, restorePagedQuerySpec);
  }

  @Override
  public RestoreKeyspacePagedResp pageListRestoreKeyspaces(
      Http.Request request,
      UUID cUUID,
      UUID rUUID,
      RestoreKeyspacePagedQuerySpec restoreKeyspacePagedQuerySpec)
      throws Exception {
    return handler.pageListRestoreKeyspaces(cUUID, rUUID, restoreKeyspacePagedQuerySpec);
  }
}
