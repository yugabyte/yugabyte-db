// Copyright (c) YugaByte, Inc.
package api.v2.controllers;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseResp;
import api.v2.models.YBPTask;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http.Request;

public class UniverseManagementApiControllerImp
    extends UniverseManagementApiControllerImpInterface {
  @Inject private UniverseManagementHandler universeHandler;

  @Override
  public UniverseResp getUniverse(Request request, UUID cUUID, UUID uniUUID) throws Exception {
    return universeHandler.getUniverse(cUUID, uniUUID);
  }

  @Override
  public YBPTask createUniverse(Request request, UUID cUUID, UniverseCreateSpec universeSpec)
      throws Exception {
    return universeHandler.createUniverse(cUUID, universeSpec);
  }

  @Override
  public YBPTask editUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditSpec universeEditSpec)
      throws Exception {
    return universeHandler.editUniverse(cUUID, uniUUID, universeEditSpec);
  }
}
