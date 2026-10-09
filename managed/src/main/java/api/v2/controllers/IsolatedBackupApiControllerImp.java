// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.IsolatedBackupHandler;
import api.v2.models.IsolatedBackupCreateSpec;
import api.v2.models.IsolatedBackupRestoreSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class IsolatedBackupApiControllerImp extends IsolatedBackupApiControllerImpInterface {
  private final IsolatedBackupHandler ibHandler;

  @Inject
  public IsolatedBackupApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      IsolatedBackupHandler ibHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.ibHandler = ibHandler;
  }

  @Override
  public YBATask createYbaBackup(
      Http.Request request, UUID cUUID, IsolatedBackupCreateSpec isolatedBackupCreateSpec) {
    return ibHandler.createYbaBackup(cUUID, isolatedBackupCreateSpec);
  }

  @Override
  public YBATask restoreYbaBackup(
      Http.Request request, UUID cUUID, IsolatedBackupRestoreSpec isolatedBackupRestoreSpec) {
    return ibHandler.restoreYbaBackup(cUUID, isolatedBackupRestoreSpec);
  }
}
