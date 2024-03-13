// Copyright (c) YugaByte, Inc.
package api.v2.controllers;

import api.v2.handlers.UniverseUpgradesManagementHandler;
import api.v2.models.UpgradeUniverseGFlags;
import api.v2.models.YBPTask;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http;

public class UniverseUpgradesManagementApiControllerImp
    extends UniverseUpgradesManagementApiControllerImpInterface {
  @Inject private UniverseUpgradesManagementHandler universeUpgradeHandler;

  @Override
  public YBPTask upgradeGFlags(
      Http.Request request, UUID cUUID, UUID uniUUID, UpgradeUniverseGFlags upgradeGFlags)
      throws Exception {
    return universeUpgradeHandler.upgradeGFlags(request, cUUID, uniUUID, upgradeGFlags);
  }
}
