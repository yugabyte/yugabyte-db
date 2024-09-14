// Copyright (c) YugaByte, Inc.

package api.v2.controllers;

import api.v2.handlers.IsolatedBackupHandler;
import api.v2.models.IsolatedBackupCreateSpec;
import api.v2.models.IsolatedBackupRestoreSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http;

public class IsolatedBackupApiControllerImp extends IsolatedBackupApiControllerImpInterface {
  @Inject private IsolatedBackupHandler ibHandler;

  @Override
  public YBATask createYbaBackup(
      Http.Request request, UUID cUUID, IsolatedBackupCreateSpec isolatedBackupCreateSpec)
      throws Exception {
    return ibHandler.createYbaBackup(request, cUUID, isolatedBackupCreateSpec);
  }

  @Override
  public YBATask restoreYbaBackup(
      Http.Request request, UUID cUUID, IsolatedBackupRestoreSpec isolatedBackupRestoreSpec)
      throws Exception {
    return ibHandler.restoreYbaBackup(request, cUUID, isolatedBackupRestoreSpec);
  }
}
