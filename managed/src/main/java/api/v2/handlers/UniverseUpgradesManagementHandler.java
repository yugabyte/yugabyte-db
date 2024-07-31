// Copyright (c) Yugabyte, Inc.
package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import api.v2.mappers.UniverseCertsRotateParamsMapper;
import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.mappers.UniverseEditGFlagsMapper;
import api.v2.mappers.UniverseEditKubernetesOverridesParamsMapper;
import api.v2.mappers.UniverseRestartParamsMapper;
import api.v2.mappers.UniverseRollbackUpgradeMapper;
import api.v2.mappers.UniverseSoftwareFinalizeMapper;
import api.v2.mappers.UniverseSoftwareFinalizeRespMapper;
import api.v2.mappers.UniverseSoftwareUpgradePrecheckMapper;
import api.v2.mappers.UniverseSoftwareUpgradeStartMapper;
import api.v2.mappers.UniverseSystemdUpgradeMapper;
import api.v2.mappers.UniverseThirdPartySoftwareUpgradeMapper;
import api.v2.mappers.UniverseTlsToggleParamsMapper;
import api.v2.models.UniverseCertRotateSpec;
import api.v2.models.UniverseEditEncryptionInTransit;
import api.v2.models.UniverseEditGFlags;
import api.v2.models.UniverseEditKubernetesOverrides;
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
import api.v2.utils.ApiControllerUtils;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoRequest;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoResponse;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class UniverseUpgradesManagementHandler extends ApiControllerUtils {
  @Inject public UpgradeUniverseHandler v1Handler;
  @Inject public Commissioner commissioner;
  @Inject private RuntimeConfGetter confGetter;

  public YBATask editGFlags(UUID cUUID, UUID uniUUID, UniverseEditGFlags editGFlags)
      throws JsonProcessingException {
    log.info("Starting v2 edit GFlags with {}", editGFlags);

    // get universe from db
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    GFlagsUpgradeParams v1Params = null;
    if (Util.isKubernetesBasedUniverse(universe)) {
      v1Params =
          UniverseDefinitionTaskParamsMapper.INSTANCE.toKubernetesGFlagsUpgradeParams(
              universe.getUniverseDetails());
    } else {
      v1Params =
          UniverseDefinitionTaskParamsMapper.INSTANCE.toGFlagsUpgradeParams(
              universe.getUniverseDetails());
    }
    // fill in SpecificGFlags from universeGFlags params into v1Params
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(editGFlags, v1Params);
    // invoke v1 upgrade api UpgradeUniverseHandler.upgradeGFlags
    UUID taskUuid = v1Handler.upgradeGFlags(v1Params, customer, universe);
    // construct a v2 Task to return from here
    YBATask YBATask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started gflags upgrade task {}", mapper.writeValueAsString(YBATask));
    return YBATask;
  }

  public YBATask startSoftwareUpgrade(
      UUID cUUID, UUID uniUUID, UniverseSoftwareUpgradeStart upgradeStart)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    SoftwareUpgradeParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toSoftwareUpgradeParams(
            universe.getUniverseDetails());

    UniverseSoftwareUpgradeStartMapper.INSTANCE.copyToV1SoftwareUpgradeParams(
        upgradeStart, v1Params);

    UUID taskUuid = null;
    if (upgradeStart.getAllowRollback()) {
      taskUuid = v1Handler.upgradeDBVersion(v1Params, customer, universe);
    } else {
      taskUuid = v1Handler.upgradeSoftware(v1Params, customer, universe);
    }
    // construct a v2 Task to return from here
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started software upgrade task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public YBATask finalizeSoftwareUpgrade(
      UUID cUUID, UUID uniUUID, UniverseSoftwareUpgradeFinalize upgradeStart)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    FinalizeUpgradeParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toFinalizeUpgradeParams(
            universe.getUniverseDetails());
    UniverseSoftwareFinalizeMapper.INSTANCE.copyToV1FinalizeUpgradeParams(upgradeStart, v1Params);

    UUID taskUuid = v1Handler.finalizeUpgrade(v1Params, customer, universe);
    // construct a v2 Task to return from here
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started finalize software upgrade task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public UniverseSoftwareUpgradeFinalizeInfo getSoftwareUpgradeFinalizeInfo(
      UUID cUUID, UUID uniUUID) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe.getOrBadRequest(uniUUID, customer);

    FinalizeUpgradeInfoResponse v1Resp = v1Handler.finalizeUpgradeInfo(cUUID, uniUUID);
    UniverseSoftwareUpgradeFinalizeInfo info =
        UniverseSoftwareFinalizeRespMapper.INSTANCE.toV2UniverseSoftwareFinalizeInfo(v1Resp);

    return info;
  }

  public YBATask startThirdPartySoftwareUpgrade(
      UUID cUUID, UUID uniUUID, UniverseThirdPartySoftwareUpgradeStart upgradeStart)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    ThirdpartySoftwareUpgradeParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toThirdpartySoftwareUpgradeParams(
            universe.getUniverseDetails());

    UniverseThirdPartySoftwareUpgradeMapper.INSTANCE.copyToV1ThirdpartySoftwareUpgradeParams(
        upgradeStart, v1Params);

    UUID taskUuid = v1Handler.thirdpartySoftwareUpgrade(v1Params, customer, universe);
    // construct a v2 Task to return from here
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started thirdparty software upgrade task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public YBATask rollbackSoftwareUpgrade(UUID cUUID, UUID uniUUID, UniverseRollbackUpgradeReq req)
      throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    RollbackUpgradeParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toRollbackUpgradeParams(
            universe.getUniverseDetails());
    UniverseRollbackUpgradeMapper.INSTANCE.copyToV1RollbackUpgradeParams(req, v1Params);
    UUID taskUuid = v1Handler.rollbackUpgrade(v1Params, customer, universe);
    // construct a v2 Task to return from here
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started software rollback task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public UniverseSoftwareUpgradePrecheckResp precheckSoftwareUpgrade(
      UUID cUUID, UUID uniUUID, UniverseSoftwareUpgradePrecheckReq precheckReq) throws Exception {
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      Release.getByVersionOrBadRequest(precheckReq.getYbSoftwareVersion());
    }
    SoftwareUpgradeInfoRequest v1Params =
        UniverseSoftwareUpgradePrecheckMapper.INSTANCE.toSoftwareUpgradeInfoRequest(precheckReq);
    SoftwareUpgradeInfoResponse v1Resp = v1Handler.softwareUpgradeInfo(cUUID, uniUUID, v1Params);
    return UniverseSoftwareUpgradePrecheckMapper.INSTANCE.toUniverseSoftwareUpgradePrecheckResp(
        v1Resp);
  }

  public YBATask restartUniverse(UUID cUUID, UUID uniUUID, UniverseRestart uniRestart)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    UUID taskUuid = null;
    if (uniRestart == null) {
      uniRestart = new UniverseRestart();
    }
    // Kubernetes services only can do a service level restart.
    if (universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(Common.CloudType.kubernetes)
        || uniRestart.getRestartType().equals(UniverseRestart.RestartTypeEnum.SERVICE)) {
      log.debug("performing universe restart (service only)");
      RestartTaskParams v1Params =
          UniverseDefinitionTaskParamsMapper.INSTANCE.toRestartTaskParams(
              universe.getUniverseDetails());
      UniverseRestartParamsMapper.INSTANCE.copyToV1RestartTaskParams(uniRestart, v1Params);
      taskUuid = v1Handler.restartUniverse(v1Params, customer, universe);
    } else if (uniRestart.getRestartType().equals(UniverseRestart.RestartTypeEnum.OS)) {
      // Soft reboot can use the v1 handler still
      log.debug("performing universe reboot (SOFT)");
      UpgradeTaskParams v1Params =
          UniverseDefinitionTaskParamsMapper.INSTANCE.toUpgradeTaskParams(
              universe.getUniverseDetails());
      UniverseRestartParamsMapper.INSTANCE.copyToV1UpgradeTaskParams(uniRestart, v1Params);
      taskUuid = v1Handler.rebootUniverse(v1Params, customer, universe);
    } else {
      log.debug("performing universe reboot (HARD)");
      throw new UnsupportedOperationException("HARD reboots are not supported in v2");
    }
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());
    log.info("Started universe restart task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public YBATask systemdEnable(UUID cUUID, UUID uniUUID, UniverseSystemdEnableStart systemd)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd) {
      throw new PlatformServiceException(BAD_REQUEST, "universe already using systemd services");
    }

    SystemdUpgradeParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toSystemdUpgradeParams(
            universe.getUniverseDetails());
    UniverseSystemdUpgradeMapper.INSTANCE.copToV1SystemdUpgradeParams(systemd, v1Params);

    UUID taskUUID = v1Handler.upgradeSystemd(v1Params, customer, universe);
    YBATask ybaTask = new YBATask().taskUuid(taskUUID).resourceUuid(uniUUID);
    log.info("Started systemd enable task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public YBATask tlsToggle(UUID cUUID, UUID uniUUID, UniverseEditEncryptionInTransit spec)
      throws JsonProcessingException {

    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    TlsToggleParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toTlsToggleParams(
            universe.getUniverseDetails());
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(spec, v1Params);

    UUID taskUUID = v1Handler.toggleTls(v1Params, customer, universe);
    YBATask ybaTask = new YBATask().taskUuid(taskUUID).resourceUuid(uniUUID);
    log.info("Started tls toggle task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public YBATask certRotate(UUID cUUID, UUID uniUUID, UniverseCertRotateSpec spec)
      throws JsonProcessingException {

    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    CertsRotateParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toCertsRotateParams(
            universe.getUniverseDetails());
    v1Params = UniverseCertsRotateParamsMapper.INSTANCE.copyToV1CertsRotateParams(spec, v1Params);

    UUID taskUUID = v1Handler.rotateCerts(v1Params, customer, universe);
    YBATask ybaTask = new YBATask().taskUuid(taskUUID).resourceUuid(uniUUID);
    log.info("Started cert rotate task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }

  public YBATask editKubernetesOverrides(
      UUID cUUID, UUID uniUUID, UniverseEditKubernetesOverrides spec)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    KubernetesOverridesUpgradeParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toKubernetesOverridesUpgradeParams(
            universe.getUniverseDetails());
    v1Params =
        UniverseEditKubernetesOverridesParamsMapper.INSTANCE.copyToV1KubernetesOverridesParams(
            spec, v1Params);

    UUID taskUUID = v1Handler.upgradeKubernetesOverrides(v1Params, customer, universe);
    YBATask ybaTask = new YBATask().taskUuid(taskUUID).resourceUuid(uniUUID);
    log.info("Started kubernetes overrides upgrade task {}", mapper.writeValueAsString(ybaTask));
    return ybaTask;
  }
}
