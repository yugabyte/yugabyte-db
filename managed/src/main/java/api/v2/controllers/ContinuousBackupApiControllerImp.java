// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.ContinuousBackupHandler;
import api.v2.models.ContinuousBackup;
import api.v2.models.ContinuousBackupSpec;
import api.v2.models.ContinuousRestoreSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class ContinuousBackupApiControllerImp extends ContinuousBackupApiControllerImpInterface {
  private final ContinuousBackupHandler cbHandler;

  @Inject
  public ContinuousBackupApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      ContinuousBackupHandler cbHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.cbHandler = cbHandler;
  }

  @Override
  public ContinuousBackup createContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousBackupSpec continuousBackupCreateSpec)
      throws Exception {
    return cbHandler.createContinuousBackup(cUUID, continuousBackupCreateSpec);
  }

  @Override
  public ContinuousBackup deleteContinuousBackup(Http.Request request, UUID cUUID, UUID bUUID)
      throws Exception {
    return cbHandler.deleteContinuousBackup(bUUID);
  }

  @Override
  public ContinuousBackup editContinuousBackup(
      Http.Request request, UUID cUUID, UUID bUUID, ContinuousBackupSpec continuousBackupCreateSpec)
      throws Exception {
    return cbHandler.editContinuousBackup(cUUID, bUUID, continuousBackupCreateSpec);
  }

  @Override
  public ContinuousBackup getContinuousBackup(Http.Request request, UUID cUUID) throws Exception {
    return cbHandler.getContinuousBackup();
  }

  @Override
  public YBATask restoreContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousRestoreSpec continuousBackupRestoreSpec)
      throws Exception {
    return cbHandler.restoreContinuousBackup(cUUID, continuousBackupRestoreSpec);
  }
}
