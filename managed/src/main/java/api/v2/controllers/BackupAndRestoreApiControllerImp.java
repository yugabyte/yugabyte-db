// Copyright (c) YugaByte, Inc.

package api.v2.controllers;

import api.v2.handlers.BackupAndRestoreHandler;
import api.v2.models.GflagMetadata;
import com.google.inject.Inject;
import java.util.List;
import play.mvc.Http;

public class BackupAndRestoreApiControllerImp extends BackupAndRestoreApiControllerImpInterface {

  @Inject BackupAndRestoreHandler backupAndRestoreHandler;

  @Override
  public List<GflagMetadata> listYbcGflagsMetadata(Http.Request request) throws Exception {
    return backupAndRestoreHandler.listYbcGflagsMetadata(request);
  }
}
