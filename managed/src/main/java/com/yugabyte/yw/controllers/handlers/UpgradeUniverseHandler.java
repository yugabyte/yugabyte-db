// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.AuditLogConfigParams;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeWithGFlags;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoRequest;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoResponse;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import play.libs.Json;

@Slf4j
@Singleton
public class UpgradeUniverseHandler {

  private final Commissioner commissioner;
  private final KubernetesManagerFactory kubernetesManagerFactory;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final YbcManager ybcManager;
  private final RuntimeConfGetter confGetter;
  private final CertificateHelper certificateHelper;
  private final AutoFlagUtil autoFlagUtil;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  public UpgradeUniverseHandler(
      Commissioner commissioner,
      KubernetesManagerFactory kubernetesManagerFactory,
      RuntimeConfigFactory runtimeConfigFactory,
      YbcManager ybcManager,
      RuntimeConfGetter confGetter,
      CertificateHelper certificateHelper,
      AutoFlagUtil autoFlagUtil,
      XClusterUniverseService xClusterUniverseService) {
    this.commissioner = commissioner;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.ybcManager = ybcManager;
    this.confGetter = confGetter;
    this.certificateHelper = certificateHelper;
    this.autoFlagUtil = autoFlagUtil;
    this.xClusterUniverseService = xClusterUniverseService;
  }

  public UUID restartUniverse(
      RestartTaskParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe, true);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    return submitUpgradeTask(
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.RestartUniverseKubernetesUpgrade
            : TaskType.RestartUniverse,
        CustomerTask.TaskType.RestartUniverse,
        requestParams,
        customer,
        universe);
  }

  public UUID upgradeSoftware(
      SoftwareUpgradeParams requestParams, Customer customer, Universe universe) {
    // Temporary fix for PLAT-4791 until PLAT-4653 fixed.
    if (universe.getUniverseDetails().getReadOnlyClusters().size() > 0
        && requestParams.getReadOnlyClusters().size() == 0) {
      requestParams.clusters.add(universe.getUniverseDetails().getReadOnlyClusters().get(0));
    }
    // Verify request params
    requestParams.verifyParams(universe, true);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    requestParams.ybPrevSoftwareVersion = userIntent.ybSoftwareVersion;

    if (userIntent.providerType.equals(CloudType.kubernetes)) {
      checkHelmChartExists(requestParams.ybSoftwareVersion);
    }

    if (userIntent.providerType.equals(CloudType.kubernetes)) {
      Provider p = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
      if (confGetter.getConfForScope(p, ProviderConfKeys.enableYbcOnK8s)
          && Util.compareYbVersions(
                  requestParams.ybSoftwareVersion, Util.K8S_YBC_COMPATIBLE_DB_VERSION, true)
              >= 0
          && !universe.isYbcEnabled()
          && requestParams.isEnableYbc()) {
        requestParams.setYbcSoftwareVersion(ybcManager.getStableYbcVersion());
        requestParams.installYbc = true;
      } else if (universe.isYbcEnabled()) {
        requestParams.setEnableYbc(true);
        requestParams.installYbc = true;
        requestParams.setYbcSoftwareVersion(ybcManager.getStableYbcVersion());
      } else {
        requestParams.setEnableYbc(false);
        requestParams.installYbc = false;
      }
    } else if (Util.compareYbVersions(
                requestParams.ybSoftwareVersion,
                confGetter.getGlobalConf(GlobalConfKeys.ybcCompatibleDbVersion),
                true)
            > 0
        && !universe.isYbcEnabled()
        && requestParams.isEnableYbc()) {
      requestParams.setYbcSoftwareVersion(ybcManager.getStableYbcVersion());
      requestParams.installYbc = true;
    } else {
      requestParams.setYbcSoftwareVersion(universe.getUniverseDetails().getYbcSoftwareVersion());
      requestParams.installYbc = false;
      requestParams.setEnableYbc(false);
    }
    requestParams.setYbcInstalled(universe.isYbcEnabled());

    TaskType taskType =
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.SoftwareKubernetesUpgrade
            : TaskType.SoftwareUpgrade;

    String currentVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (confGetter.getConfForScope(universe, UniverseConfKeys.enableRollbackSupport)
        && CommonUtils.isReleaseEqualOrAfter(
            Util.YBDB_ROLLBACK_DB_VERSION, requestParams.ybSoftwareVersion)
        && CommonUtils.isReleaseEqualOrAfter(Util.YBDB_ROLLBACK_DB_VERSION, currentVersion)) {
      taskType =
          taskType.equals(TaskType.SoftwareUpgrade)
              ? TaskType.SoftwareUpgradeYB
              : TaskType.SoftwareKubernetesUpgradeYB;
    }
    return submitUpgradeTask(
        taskType, CustomerTask.TaskType.SoftwareUpgrade, requestParams, customer, universe);
  }

  public UUID finalizeUpgrade(
      FinalizeUpgradeParams requestParams, Customer customer, Universe universe) {

    requestParams.verifyParams(universe, true);

    return submitUpgradeTask(
        TaskType.FinalizeUpgrade,
        CustomerTask.TaskType.FinalizeUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID rollbackUpgrade(
      RollbackUpgradeParams requestParams, Customer customer, Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    TaskType taskType =
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.RollbackKubernetesUpgrade
            : TaskType.RollbackUpgrade;

    requestParams.verifyParams(universe, true);
    requestParams.ybPrevSoftwareVersion = userIntent.ybSoftwareVersion;
    PrevYBSoftwareConfig prevYBSoftwareConfig = universe.getUniverseDetails().prevYBSoftwareConfig;
    if (prevYBSoftwareConfig != null) {
      requestParams.ybSoftwareVersion = prevYBSoftwareConfig.getSoftwareVersion();
    } else {
      requestParams.ybSoftwareVersion = userIntent.ybSoftwareVersion;
    }

    return submitUpgradeTask(
        taskType, CustomerTask.TaskType.RollbackUpgrade, requestParams, customer, universe);
  }

  public UUID upgradeGFlags(
      GFlagsUpgradeParams requestParams, Customer customer, Universe universe) {
    UserIntent userIntent;
    if (MapUtils.isEmpty(requestParams.masterGFlags)
        && MapUtils.isEmpty(requestParams.tserverGFlags)
        && requestParams.getPrimaryCluster() != null) {
      // If user hasn't provided gflags in the top level params, get from primary cluster
      userIntent = requestParams.getPrimaryCluster().userIntent;
      userIntent.masterGFlags = GFlagsUtil.trimFlags(userIntent.masterGFlags);
      userIntent.tserverGFlags = GFlagsUtil.trimFlags(userIntent.tserverGFlags);
      requestParams.masterGFlags = userIntent.masterGFlags;
      requestParams.tserverGFlags = userIntent.tserverGFlags;
    } else {
      userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      requestParams.masterGFlags = GFlagsUtil.trimFlags(requestParams.masterGFlags);
      requestParams.tserverGFlags = GFlagsUtil.trimFlags((requestParams.tserverGFlags));
    }

    // Temporary fix for PLAT-4791 until PLAT-4653 fixed.
    if (universe.getUniverseDetails().getReadOnlyClusters().size() > 0
        && requestParams.getReadOnlyClusters().size() == 0) {
      requestParams.clusters.add(universe.getUniverseDetails().getReadOnlyClusters().get(0));
    }
    // Verify request params
    try {
      requestParams.verifyParams(universe, true);
    } catch (PlatformServiceException p) {
      if (!p.getMessage().equals(UpgradeWithGFlags.SPECIFIC_GFLAGS_NO_CHANGES_ERROR)
          || !checkGFlagsProvidedInOldSchema(universe, requestParams)) {
        throw p;
      } else { // Used old maps to upgrade cluster in new scheme.
        // Verify params again (to check for other errors)
        requestParams.verifyParams(universe, true);
      }
    }
    if (userIntent.providerType.equals(CloudType.kubernetes)) {
      // Gflags upgrade does not change universe version. Check for current version of helm chart.
      checkHelmChartExists(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    }
    return submitUpgradeTask(
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.GFlagsKubernetesUpgrade
            : TaskType.GFlagsUpgrade,
        CustomerTask.TaskType.GFlagsUpgrade,
        requestParams,
        customer,
        universe);
  }

  /**
   * To support old API for UI users, we can check if user provided gflags in old manner, and sync
   * them into specificGFlags
   *
   * @param universe
   * @param requestParams
   * @return true if successfully synchronized old fields into specificGFlags.
   */
  private boolean checkGFlagsProvidedInOldSchema(
      Universe universe, GFlagsUpgradeParams requestParams) {
    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);
    Map<String, String> oldMasterGFlags =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags;
    Map<String, String> oldTserverGFlags =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags;

    if (!cloudEnabled
        && requestParams.masterGFlags != null
        && requestParams.tserverGFlags != null
        && (!Objects.equals(requestParams.masterGFlags, oldMasterGFlags)
            || !Objects.equals(requestParams.tserverGFlags, oldTserverGFlags))) {
      UniverseDefinitionTaskParams.Cluster primaryCluster =
          universe.getUniverseDetails().getPrimaryCluster();
      Map<String, String> prevMasterGFlags =
          GFlagsUtil.getBaseGFlags(
              UniverseTaskBase.ServerType.MASTER,
              primaryCluster,
              universe.getUniverseDetails().clusters);
      Map<String, String> prevTserverGFlags =
          GFlagsUtil.getBaseGFlags(
              UniverseTaskBase.ServerType.TSERVER,
              primaryCluster,
              universe.getUniverseDetails().clusters);
      SpecificGFlags primarySpecificGFlags =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags;
      if (!Objects.equals(requestParams.masterGFlags, prevMasterGFlags)
          || !Objects.equals(requestParams.tserverGFlags, prevTserverGFlags)) {
        if (primarySpecificGFlags.hasPerAZOverrides()) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Cannot upgrade gflags using old fields because there are overrides per az"
                  + " in primary cluster. Please modify specificGFlags to do upgrade.");
        }
        if (!universe.getUniverseDetails().getReadOnlyClusters().isEmpty()) {
          SpecificGFlags specificGFlagsForRR =
              universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.specificGFlags;
          if (specificGFlagsForRR.hasPerAZOverrides()) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Cannot upgrade gflags using old fields because there are overrides per az"
                    + " in readonly cluster. Please modify specificGFlags to do upgrade.");
          }
          if (specificGFlagsForRR != null && !specificGFlagsForRR.isInheritFromPrimary()) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Cannot upgrade gflags using old fields because read replica has overriden "
                    + "gflags. Please modify specificGFlags to do upgrade.");
          }
        }
        UniverseDefinitionTaskParams.Cluster requestParamsCluster =
            requestParams.getPrimaryCluster();
        log.warn(
            "GFlag request params are specified in old manner: specificGFlags: {}, masterGFlags:"
                + " {}, tserverGFlags: {}",
            Json.toJson(requestParamsCluster.userIntent.specificGFlags),
            requestParams.masterGFlags,
            requestParams.tserverGFlags);
        requestParamsCluster.userIntent.specificGFlags =
            SpecificGFlags.construct(requestParams.masterGFlags, requestParams.tserverGFlags);
        return true;
      }
    }
    return false;
  }

  public UUID upgradeKubernetesOverrides(
      KubernetesOverridesUpgradeParams requestParams, Customer customer, Universe universe) {
    // Temporary fix for PLAT-4791 until PLAT-4653 fixed.
    if (universe.getUniverseDetails().getReadOnlyClusters().size() > 0
        && requestParams.getReadOnlyClusters().size() == 0) {
      requestParams.clusters.add(universe.getUniverseDetails().getReadOnlyClusters().get(0));
    }
    requestParams.verifyParams(universe, true);
    return submitUpgradeTask(
        TaskType.KubernetesOverridesUpgrade,
        CustomerTask.TaskType.KubernetesOverridesUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID rotateCerts(CertsRotateParams requestParams, Customer customer, Universe universe) {
    log.debug(
        "rotateCerts called with rootCA: {}",
        (requestParams.rootCA != null) ? requestParams.rootCA.toString() : "NULL");
    // Temporary fix for PLAT-4791 until PLAT-4653 fixed.
    if (universe.getUniverseDetails().getReadOnlyClusters().size() > 0
        && requestParams.getReadOnlyClusters().size() == 0) {
      requestParams.clusters.add(universe.getUniverseDetails().getReadOnlyClusters().get(0));
    }
    // Verify request params
    requestParams.verifyParams(universe, true);
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
    // This is there only for legacy support, no need if rootCA and clientRootCA are different.
    if (userIntent.enableClientToNodeEncrypt && requestParams.rootAndClientRootCASame) {
      CertificateInfo rootCert = CertificateInfo.get(requestParams.rootCA);
      if (rootCert.getCertType() == CertConfigType.SelfSigned
          || rootCert.getCertType() == CertConfigType.HashicorpVault) {
        CertificateHelper.createClientCertificate(
            runtimeConfigFactory.staticApplicationConf(), customer.getUuid(), requestParams.rootCA);
      }
    }

    if (userIntent.providerType.equals(CloudType.kubernetes)) {
      // Certs rotate does not change universe version. Check for current version of helm chart.
      checkHelmChartExists(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    }

    return submitUpgradeTask(
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.CertsRotateKubernetesUpgrade
            : TaskType.CertsRotate,
        CustomerTask.TaskType.CertsRotate,
        requestParams,
        customer,
        universe);
  }

  public UUID resizeNode(ResizeNodeParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe, true);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    return submitUpgradeTask(
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.UpdateKubernetesDiskSize
            : TaskType.ResizeNode,
        CustomerTask.TaskType.ResizeNode,
        requestParams,
        customer,
        universe);
  }

  public UUID thirdpartySoftwareUpgrade(
      ThirdpartySoftwareUpgradeParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe, true);
    return submitUpgradeTask(
        TaskType.ThirdpartySoftwareUpgrade,
        CustomerTask.TaskType.ThirdpartySoftwareUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID modifyAuditLoggingConfig(
      AuditLogConfigParams requestParams, Customer customer, Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;

    requestParams.verifyParams(universe, true);
    userIntent.auditLogConfig = requestParams.auditLogConfig;
    return submitUpgradeTask(
        TaskType.ModifyAuditLoggingConfig,
        CustomerTask.TaskType.ModifyAuditLoggingConfig,
        requestParams,
        customer,
        universe);
  }

  // Enable/Disable TLS on Cluster
  public UUID toggleTls(TlsToggleParams requestParams, Customer customer, Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;

    // Verify request params
    requestParams.verifyParams(universe, true);
    requestParams.allowInsecure =
        !(requestParams.enableNodeToNodeEncrypt || requestParams.enableClientToNodeEncrypt);

    if (requestParams.enableNodeToNodeEncrypt) {
      // Setting the rootCA to the already existing rootCA as we do not
      // support root certificate rotation through TLS upgrade.
      // There is a check for different new and existing root cert already.
      if (universeDetails.rootCA == null) {
        // Create self-signed rootCA in case it is not provided by the user
        if (requestParams.rootCA == null) {
          requestParams.rootCA =
              certificateHelper.createRootCA(
                  runtimeConfigFactory.staticApplicationConf(),
                  universeDetails.nodePrefix,
                  customer.getUuid());
        }
      } else {
        // If certificate already present then use the same as upgrade cannot rotate certs
        requestParams.rootCA = universeDetails.rootCA;
      }
    }

    if (requestParams.enableClientToNodeEncrypt) {
      // Setting the ClientRootCA to the already existing clientRootCA as we do not
      // support root certificate rotation through TLS upgrade.
      // There is a check for different new and existing root cert already.
      if (universeDetails.getClientRootCA() == null) {
        if (requestParams.getClientRootCA() == null) {
          if (requestParams.rootCA != null && requestParams.rootAndClientRootCASame) {
            // Setting ClientRootCA to RootCA in case rootAndClientRootCA is true
            requestParams.setClientRootCA(requestParams.rootCA);
          } else {
            // Create self-signed clientRootCA in case it is not provided by the user
            // and rootCA and clientRootCA needs to be different
            requestParams.setClientRootCA(
                certificateHelper.createClientRootCA(
                    runtimeConfigFactory.staticApplicationConf(),
                    universeDetails.nodePrefix,
                    customer.getUuid()));
          }
        }
      } else {
        requestParams.setClientRootCA(universeDetails.getClientRootCA());
      }

      // Setting rootCA to ClientRootCA in case node to node encryption is disabled.
      // This is necessary to set to ensure backward compatibility as existing parts of
      // codebase uses rootCA for Client to Node Encryption
      if (requestParams.rootCA == null && requestParams.rootAndClientRootCASame) {
        requestParams.rootCA = requestParams.getClientRootCA();
      }

      // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
      // This is there only for legacy support, no need if rootCA and clientRootCA are different.
      if (requestParams.rootAndClientRootCASame) {
        CertificateInfo cert = CertificateInfo.get(requestParams.rootCA);
        if (cert.getCertType() == CertConfigType.SelfSigned
            || cert.getCertType() == CertConfigType.HashicorpVault) {
          CertificateHelper.createClientCertificate(
              runtimeConfigFactory.staticApplicationConf(),
              customer.getUuid(),
              requestParams.rootCA);
        }
      }
    }

    String typeName = generateTypeName(userIntent, requestParams);

    return submitUpgradeTask(
        TaskType.TlsToggle,
        CustomerTask.TaskType.TlsToggle,
        requestParams,
        customer,
        universe,
        typeName);
  }

  public UUID upgradeVMImage(
      VMImageUpgradeParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe, true);
    return submitUpgradeTask(
        TaskType.VMImageUpgrade,
        CustomerTask.TaskType.VMImageUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID upgradeSystemd(
      SystemdUpgradeParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe, true);

    return submitUpgradeTask(
        TaskType.SystemdUpgrade,
        CustomerTask.TaskType.SystemdUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID rebootUniverse(
      UpgradeTaskParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe, true);

    return submitUpgradeTask(
        TaskType.RebootUniverse,
        CustomerTask.TaskType.RebootUniverse,
        requestParams,
        customer,
        universe);
  }

  public SoftwareUpgradeInfoResponse softwareUpgradeInfo(
      UUID customerUUID, UUID universeUUID, SoftwareUpgradeInfoRequest request) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String currentVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    String newVersion = request.getYbSoftwareVersion();
    if (!CommonUtils.isReleaseEqualOrAfter(Util.YBDB_ROLLBACK_DB_VERSION, currentVersion)
        || !CommonUtils.isReleaseEqualOrAfter(Util.YBDB_ROLLBACK_DB_VERSION, newVersion)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No software upgrade info available for this universe");
    }
    SoftwareUpgradeInfoResponse response = new SoftwareUpgradeInfoResponse();
    try {
      response.setFinalizeRequired(autoFlagUtil.upgradeRequireFinalize(currentVersion, newVersion));
    } catch (IOException e) {
      log.error("Error: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while checking auto-finalize for upgrade.");
    }
    return response;
  }

  public FinalizeUpgradeInfoResponse finalizeUpgradeInfo(UUID customerUUID, UUID universeUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    FinalizeUpgradeInfoResponse response = new FinalizeUpgradeInfoResponse();
    if (!CommonUtils.isReleaseEqualOrAfter(
        Util.YBDB_ROLLBACK_DB_VERSION,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No finalize upgrade info available for this universe");
    }
    try {
      response.setImpactedXClusterConnectedUniverse(
          xClusterUniverseService.getXClusterTargetUniverseSetToBeImpactedWithUpgradeFinalize(
              universe));
    } catch (IOException e) {
      log.error("Error: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching impacted xCluster connected universe set.");
    }
    return response;
  }

  private UUID submitUpgradeTask(
      TaskType taskType,
      CustomerTask.TaskType customerTaskType,
      UpgradeTaskParams upgradeTaskParams,
      Customer customer,
      Universe universe) {
    return submitUpgradeTask(
        taskType, customerTaskType, upgradeTaskParams, customer, universe, null);
  }

  private UUID submitUpgradeTask(
      TaskType taskType,
      CustomerTask.TaskType customerTaskType,
      UpgradeTaskParams upgradeTaskParams,
      Customer customer,
      Universe universe,
      String customTaskName) {
    UUID taskUUID = commissioner.submit(taskType, upgradeTaskParams);
    log.info(
        "Submitted {} for {} : {}, task uuid = {}.",
        taskType,
        universe.getUniverseUUID(),
        universe.getName(),
        taskUUID);

    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        customerTaskType,
        universe.getName(),
        customTaskName);
    log.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.getUniverseUUID(),
        universe.getName());
    return taskUUID;
  }

  private void checkHelmChartExists(String ybSoftwareVersion) {
    try {
      kubernetesManagerFactory.getManager().getHelmPackagePath(ybSoftwareVersion);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  @VisibleForTesting
  static String generateTypeName(UserIntent userIntent, TlsToggleParams requestParams) {
    String baseTaskName = "TLS Toggle ";
    Boolean clientToNode =
        (userIntent.enableClientToNodeEncrypt == requestParams.enableClientToNodeEncrypt)
            ? null
            : requestParams.enableClientToNodeEncrypt;
    Boolean nodeToNode =
        (userIntent.enableNodeToNodeEncrypt == requestParams.enableNodeToNodeEncrypt)
            ? null
            : requestParams.enableNodeToNodeEncrypt;
    if (clientToNode != null && nodeToNode != null && !clientToNode.equals(nodeToNode)) {
      // one is off, other is on
      baseTaskName += "Client " + booleanToStr(clientToNode) + " Node " + booleanToStr(nodeToNode);
    } else {
      baseTaskName += booleanToStr(clientToNode == null ? nodeToNode : clientToNode);
    }
    return baseTaskName;
  }

  private static String booleanToStr(boolean toggle) {
    return toggle ? "ON" : "OFF";
  }
}
