// Copyright (c) YugaByte, Inc.

package api.v2.controllers;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.handlers.UniverseUpgradesManagementHandler;
import api.v2.models.ClusterAddSpec;
import api.v2.models.Universe;
import api.v2.models.UniverseCertRotateSpec;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseDeleteSpec;
import api.v2.models.UniverseEditEncryptionInTransit;
import api.v2.models.UniverseEditGFlags;
import api.v2.models.UniverseEditKubernetesOverrides;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseRestart;
import api.v2.models.UniverseRollbackUpgradeReq;
import api.v2.models.UniverseSoftwareUpgradeFinalize;
import api.v2.models.UniverseSoftwareUpgradeFinalizeInfo;
import api.v2.models.UniverseSoftwareUpgradePrecheckReq;
import api.v2.models.UniverseSoftwareUpgradePrecheckResp;
import api.v2.models.UniverseSoftwareUpgradeStart;
import api.v2.models.UniverseSystemdEnableStart;
import api.v2.models.UniverseThirdPartySoftwareUpgradeStart;
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
    return universeHandler.createUniverse(request, cUUID, universeSpec);
  }

  @Override
  public YBATask editUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditSpec universeEditSpec)
      throws Exception {
    return universeHandler.editUniverse(request, cUUID, uniUUID, universeEditSpec);
  }

  @Override
  public YBATask addCluster(
      Request request, UUID cUUID, UUID uniUUID, ClusterAddSpec clusterAddSpec) throws Exception {
    return universeHandler.addCluster(request, cUUID, uniUUID, clusterAddSpec);
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

  @Override
  public YBATask deleteUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseDeleteSpec universeDeleteSpec)
      throws Exception {
    return universeHandler.deleteUniverse(cUUID, uniUUID, universeDeleteSpec);
  }

  @Override
  public YBATask startSoftwareUpgrade(
      Request request, UUID cUUID, UUID uniUUID, UniverseSoftwareUpgradeStart uniUpgrade)
      throws Exception {
    return universeUpgradeHandler.startSoftwareUpgrade(request, cUUID, uniUUID, uniUpgrade);
  }

  @Override
  public YBATask finalizeSoftwareUpgrade(
      Request request, UUID cUUID, UUID uniUUID, UniverseSoftwareUpgradeFinalize finalizeInfo)
      throws Exception {
    return universeUpgradeHandler.finalizeSoftwareUpgrade(request, cUUID, uniUUID, finalizeInfo);
  }

  @Override
  public UniverseSoftwareUpgradeFinalizeInfo getFinalizeSoftwareUpgradeInfo(
      Request request, UUID cUUID, UUID uniUUID) throws Exception {
    return universeUpgradeHandler.getSoftwareUpgradeFinalizeInfo(cUUID, uniUUID);
  }

  @Override
  public YBATask startThirdPartySoftwareUpgrade(
      Request request, UUID cUUID, UUID uniUUID, UniverseThirdPartySoftwareUpgradeStart uniUpgrade)
      throws Exception {
    return universeUpgradeHandler.startThirdPartySoftwareUpgrade(
        request, cUUID, uniUUID, uniUpgrade);
  }

  @Override
  public YBATask rollbackSoftwareUpgrade(
      Request request, UUID cUUID, UUID uniUUID, UniverseRollbackUpgradeReq req) throws Exception {
    return universeUpgradeHandler.rollbackSoftwareUpgrade(request, cUUID, uniUUID, req);
  }

  @Override
  public UniverseSoftwareUpgradePrecheckResp precheckSoftwareUpgrade(
      Request request, UUID cUUID, UUID uniUUID, UniverseSoftwareUpgradePrecheckReq req)
      throws Exception {
    return universeUpgradeHandler.precheckSoftwareUpgrade(cUUID, uniUUID, req);
  }

  @Override
  public YBATask restartUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseRestart uniUpgrade) throws Exception {
    return universeUpgradeHandler.restartUniverse(request, cUUID, uniUUID, uniUpgrade);
  }

  @Override
  public YBATask systemdEnable(
      Request request, UUID cUUID, UUID uniUUID, UniverseSystemdEnableStart systemd)
      throws Exception {
    return universeUpgradeHandler.systemdEnable(request, cUUID, uniUUID, systemd);
  }

  @Override
  public YBATask encryptionInTransitToggle(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditEncryptionInTransit spec)
      throws Exception {
    return universeUpgradeHandler.tlsToggle(request, cUUID, uniUUID, spec);
  }

  @Override
  public YBATask encryptionInTransitCertRotate(
      Request request, UUID cUUID, UUID uniUUID, UniverseCertRotateSpec spec) throws Exception {
    return universeUpgradeHandler.certRotate(request, cUUID, uniUUID, spec);
  }

  @Override
  public YBATask editKubernetesOverrides(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditKubernetesOverrides spec)
      throws Exception {
    return universeUpgradeHandler.editKubernetesOverrides(request, cUUID, uniUUID, spec);
  }
}
