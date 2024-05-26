// Copyright (c) YugaByte, Inc.
package api.v2.controllers;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.handlers.UniverseUpgradesManagementHandler;
import api.v2.models.ClusterAddSpec;
import api.v2.models.Universe;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseEditGFlags;
import api.v2.models.UniverseEditSpec;
import api.v2.models.YBATask;
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
  public YBATask createUniverse(Request request, UUID cUUID, UniverseCreateSpec universeSpec)
      throws Exception {
    return universeHandler.createUniverse(cUUID, universeSpec);
  }

  @Override
  public YBATask editUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditSpec universeEditSpec)
      throws Exception {
    return universeHandler.editUniverse(cUUID, uniUUID, universeEditSpec);
  }

  @Override
  public YBATask addCluster(
      Request request, UUID cUUID, UUID uniUUID, ClusterAddSpec clusterAddSpec) throws Exception {
    return universeHandler.addCluster(cUUID, uniUUID, clusterAddSpec);
  }

  @Override
  public YBATask deleteCluster(
      Request request, UUID cUUID, UUID uniUUID, UUID clsUUID, Boolean forceDelete)
      throws Exception {
    return universeHandler.deleteReadReplicaCluster(cUUID, uniUUID, clsUUID, forceDelete);
  }

  @Override
  public YBATask editGFlags(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditGFlags universeEditGFlags)
      throws Exception {
    return universeUpgradeHandler.editGFlags(request, cUUID, uniUUID, universeEditGFlags);
  }
}
