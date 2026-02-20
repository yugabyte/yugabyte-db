// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import static play.mvc.Results.ok;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.handlers.UniverseUpgradesManagementHandler;
import api.v2.models.AttachUniverseSpec;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ConfigureMetricsExportSpec;
import api.v2.models.DetachUniverseSpec;
import api.v2.models.RunScriptRequest;
import api.v2.models.RunScriptResponse;
import api.v2.models.Universe;
import api.v2.models.UniverseCertRotateSpec;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseDeleteSpec;
import api.v2.models.UniverseEditEncryptionInTransit;
import api.v2.models.UniverseEditGFlags;
import api.v2.models.UniverseEditKubernetesOverrides;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseOperatorImportReq;
import api.v2.models.UniverseQueryLogsExport;
import api.v2.models.UniverseResourceDetails;
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
import com.yugabyte.yw.models.Audit;
import java.io.InputStream;
import java.util.UUID;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;

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

  // Overrode this method to improve response handling in clients - the Content-Disposition lets the
  // client know to handle this response as a downloaded file named "attachDetachSpec.tar.gz". Also,
  // the content type is specified as "application/gzip" to set the MIME type of the response. If we
  // were to add UI for this feature without this addition, the browser might try to display the
  // file instead of downloading it. Accessing the API through curl is fine regardless.
  @Override
  public Result detachUniverseHttp(
      Http.Request request, UUID cUUID, UUID uniUUID, DetachUniverseSpec detachUniverseSpec)
      throws Exception {
    InputStream obj = detachUniverse(request, cUUID, uniUUID, detachUniverseSpec);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            uniUUID.toString(),
            Audit.ActionType.Detach,
            request.body().asJson());
    return ok(obj)
        .withHeader("Content-Disposition", "attachment; filename=attachDetachSpec.tar.gz")
        .as("application/gzip");
  }

  @Override
  public InputStream detachUniverse(
      Request request, UUID cUUID, UUID uniUUID, DetachUniverseSpec detachUniverseSpec)
      throws Exception {
    return universeHandler.detachUniverse(request, cUUID, uniUUID, detachUniverseSpec);
  }

  @Override
  public void attachUniverse(
      Request request, UUID cUUID, UUID uniUUID, AttachUniverseSpec attachUniverseSpec)
      throws Exception {
    universeHandler.attachUniverse(request, cUUID, uniUUID, attachUniverseSpec);
  }

  @Override
  public void deleteAttachDetachMetadata(Request request, UUID cUUID, UUID uniUUID)
      throws Exception {
    universeHandler.deleteAttachDetachMetadata(request, cUUID, uniUUID);
  }

  @Override
  public void rollbackDetachUniverse(
      Request request, UUID cUUID, UUID uniUUID, Boolean isForceRollback) throws Exception {
    universeHandler.rollbackDetachUniverse(request, cUUID, uniUUID, isForceRollback);
  }

  @Override
  public UniverseResourceDetails getUniverseResources(
      Request request, UUID cUUID, UniverseCreateSpec universeSpec) throws Exception {
    return universeHandler.getUniverseResources(request, cUUID, universeSpec);
  }

  @Override
  public YBATask configureQueryLogging(
      Request request, UUID cUUID, UUID uniUUID, UniverseQueryLogsExport req) throws Exception {
    return universeUpgradeHandler.configureQueryLogging(request, cUUID, uniUUID, req);
  }

  @Override
  public YBATask configureMetricsExport(
      Request request, UUID cUUID, UUID uniUUID, ConfigureMetricsExportSpec req) throws Exception {
    return universeUpgradeHandler.configureMetricsExport(request, cUUID, uniUUID, req);
  }

  @Override
  public YBATask operatorImportUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseOperatorImportReq req) throws Exception {
    return universeHandler.operatorImportUniverse(request, cUUID, uniUUID, req);
  }

  @Override
  public void operatorImportUniversePrecheck(
      Request request, UUID cUUID, UUID uniUUID, UniverseOperatorImportReq req) throws Exception {
    universeHandler.precheckOperatorImportUniverse(request, cUUID, uniUUID, req);
  }

  @Override
  public RunScriptResponse runScript(
      Request request, UUID cUUID, UUID uniUUID, RunScriptRequest runScriptRequest)
      throws Exception {
    return universeHandler.runScript(request, cUUID, uniUUID, runScriptRequest);
  }
}
