// Copyright (c) YugaByte, Inc.
package api.v2.controllers;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.handlers.UniverseUpgradesManagementHandler;
import api.v2.models.ClusterAddSpec;
import api.v2.models.Universe;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseEditGFlags;
import api.v2.models.UniverseEditSpec;
import api.v2.models.YBPTask;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http.Request;

public class UniverseApiControllerImp extends UniverseApiControllerImpInterface {
  @Inject private UniverseManagementHandler universeHandler;
  @Inject private UniverseUpgradesManagementHandler universeUpgradeHandler;

  @Override
  public Universe getUniverse(Request request, UUID cUUID, UUID uniUUID) throws Exception {
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

  @Override
  public YBPTask addCluster(
      Request request, UUID cUUID, UUID uniUUID, ClusterAddSpec clusterAddSpec) throws Exception {
    return universeHandler.addCluster(cUUID, uniUUID, clusterAddSpec);
  }

  @Override
  public YBPTask deleteCluster(
      Request request, UUID cUUID, UUID uniUUID, UUID clsUUID, Boolean forceDelete)
      throws Exception {
    return universeHandler.deleteReadReplicaCluster(cUUID, uniUUID, clsUUID, forceDelete);
  }

  @Override
  public YBPTask editGFlags(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditGFlags universeEditGFlags)
      throws Exception {
    return universeUpgradeHandler.editGFlags(request, cUUID, uniUUID, universeEditGFlags);
  }
}
