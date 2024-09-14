// Copyright (c) YugaByte, Inc.

package api.v2.controllers;

import api.v2.handlers.ContinuousBackupHandler;
import api.v2.models.ContinuousBackup;
import api.v2.models.ContinuousBackupCreateSpec;
import api.v2.models.ContinuousRestoreSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http;

public class ContinuousBackupApiControllerImp extends ContinuousBackupApiControllerImpInterface {
  @Inject private ContinuousBackupHandler cbHandler;

  @Override
  public ContinuousBackup createContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousBackupCreateSpec continuousBackupCreateSpec)
      throws Exception {
    return cbHandler.createContinuousBackup(request, cUUID, continuousBackupCreateSpec);
  }

  @Override
  public ContinuousBackup deleteContinuousBackup(Http.Request request, UUID cUUID, UUID bUUID)
      throws Exception {
    return cbHandler.deleteContinuousBackup(request, cUUID, bUUID);
  }

  @Override
  public ContinuousBackup editContinuousBackup(
      Http.Request request,
      UUID cUUID,
      UUID bUUID,
      ContinuousBackupCreateSpec continuousBackupCreateSpec)
      throws Exception {
    return cbHandler.editContinuousBackup(request, cUUID, bUUID, continuousBackupCreateSpec);
  }

  @Override
  public ContinuousBackup getContinuousBackup(Http.Request request, UUID cUUID) throws Exception {
    return cbHandler.getContinuousBackup(request, cUUID);
  }

  @Override
  public YBATask restoreContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousRestoreSpec continuousBackupRestoreSpec)
      throws Exception {
    return cbHandler.restoreContinuousBackup(request, cUUID, continuousBackupRestoreSpec);
  }
}
