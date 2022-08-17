// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagsAuditPayload;
import com.yugabyte.yw.common.gflags.GFlagDiffEntry;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YbcManager;

@Slf4j
public class UpgradeUniverseHandler {

  private final Commissioner commissioner;
  private final KubernetesManagerFactory kubernetesManagerFactory;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final GFlagsValidationHandler gFlagsValidationHandler;
  private final YbcManager ybcManager;

  @Inject
  public UpgradeUniverseHandler(
      Commissioner commissioner,
      KubernetesManagerFactory kubernetesManagerFactory,
      RuntimeConfigFactory runtimeConfigFactory,
      GFlagsValidationHandler gFlagsValidationHandler,
      YbcManager ybcManager) {
    this.commissioner = commissioner;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.gFlagsValidationHandler = gFlagsValidationHandler;
    this.ybcManager = ybcManager;
  }

  public UUID restartUniverse(
      UpgradeTaskParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    return submitUpgradeTask(
        TaskType.RestartUniverse,
        CustomerTask.TaskType.RestartUniverse,
        requestParams,
        customer,
        universe);
  }

  public UUID upgradeSoftware(
      SoftwareUpgradeParams requestParams, Customer customer, Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

    // Defaults to false, but we need to extract the variable in case the user wishes to perform
    // a downgrade with a runtime configuration override. We perform this check before verifying the
    // general
    // SoftwareUpgradeParams to avoid introducing an API parameter.
    boolean isUniverseDowngradeAllowed =
        runtimeConfigFactory.forUniverse(universe).getBoolean("yb.upgrade.allow_downgrades");

    String currentVersion = userIntent.ybSoftwareVersion;

    String desiredUpgradeVersion = requestParams.ybSoftwareVersion;

    if (currentVersion != null) {

      if (Util.compareYbVersions(currentVersion, desiredUpgradeVersion, true) > 0) {

        if (!isUniverseDowngradeAllowed) {

          String msg =
              String.format(
                  "DB version downgrades are not recommended,"
                      + " %s"
                      + " would downgrade from"
                      + " %s"
                      + ". Aborting."
                      + " To override this check and force a downgrade, please set the runtime"
                      + " config yb.upgrade.allow_downgrades"
                      + " to true"
                      + " (using the script set-runtime-config.sh if necessary).",
                  desiredUpgradeVersion, currentVersion);

          throw new PlatformServiceException(Status.BAD_REQUEST, msg);
        }
      }
    }

    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    if (userIntent.providerType.equals(CloudType.kubernetes)) {
      checkHelmChartExists(requestParams.ybSoftwareVersion);
    }

    if (Util.compareYbVersions(
                requestParams.ybSoftwareVersion, Util.YBC_COMPATIBLE_DB_VERSION, true)
            > 0
        && !universe.isYbcEnabled()
        && requestParams.enableYbc) {
      requestParams.ybcSoftwareVersion = ybcManager.getStableYbcVersion();
      requestParams.installYbc = true;
    } else {
      requestParams.ybcSoftwareVersion = universe.getUniverseDetails().ybcSoftwareVersion;
      requestParams.installYbc = false;
      requestParams.enableYbc = false;
    }
    requestParams.ybcInstalled = universe.isYbcEnabled();

    return submitUpgradeTask(
        userIntent.providerType.equals(CloudType.kubernetes)
            ? TaskType.SoftwareKubernetesUpgrade
            : TaskType.SoftwareUpgrade,
        CustomerTask.TaskType.SoftwareUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID upgradeGFlags(
      GFlagsUpgradeParams requestParams, Customer customer, Universe universe) {
    UserIntent userIntent;
    if (requestParams.masterGFlags.isEmpty()
        && requestParams.tserverGFlags.isEmpty()
        && requestParams.getPrimaryCluster() != null) {
      // If user hasn't provided gflags in the top level params, get from primary cluster
      userIntent = requestParams.getPrimaryCluster().userIntent;
      userIntent.masterGFlags = trimFlags(userIntent.masterGFlags);
      userIntent.tserverGFlags = trimFlags(userIntent.tserverGFlags);
      requestParams.masterGFlags = userIntent.masterGFlags;
      requestParams.tserverGFlags = userIntent.tserverGFlags;
    } else {
      userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      requestParams.masterGFlags = trimFlags(requestParams.masterGFlags);
      requestParams.tserverGFlags = trimFlags((requestParams.tserverGFlags));
    }

    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

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

  public JsonNode constructGFlagAuditPayload(
      GFlagsUpgradeParams requestParams, UserIntent oldUserIntent) {
    if (requestParams.getPrimaryCluster() == null) {
      return null;
    }
    UserIntent newUserIntent = requestParams.getPrimaryCluster().userIntent;
    Map<String, String> newMasterGFlags = newUserIntent.masterGFlags;
    Map<String, String> newTserverGFlags = newUserIntent.tserverGFlags;
    Map<String, String> oldMasterGFlags = oldUserIntent.masterGFlags;
    Map<String, String> oldTserverGFlags = oldUserIntent.tserverGFlags;
    GFlagsAuditPayload payload = new GFlagsAuditPayload();
    String softwareVersion = newUserIntent.ybSoftwareVersion;
    payload.master =
        generateGFlagEntries(
            oldMasterGFlags, newMasterGFlags, ServerType.MASTER.toString(), softwareVersion);
    payload.tserver =
        generateGFlagEntries(
            oldTserverGFlags, newTserverGFlags, ServerType.TSERVER.toString(), softwareVersion);

    ObjectMapper mapper = new ObjectMapper();
    Map<String, GFlagsAuditPayload> auditPayload = new HashMap<>();
    auditPayload.put("gflags", payload);

    return mapper.valueToTree(auditPayload);
  }

  public List<GFlagDiffEntry> generateGFlagEntries(
      Map<String, String> oldGFlags,
      Map<String, String> newGFlags,
      String serverType,
      String softwareVersion) {
    List<GFlagDiffEntry> gFlagChanges = new ArrayList<GFlagDiffEntry>();

    GFlagDiffEntry tEntry;
    Collection<String> modifiedGFlags = Sets.union(oldGFlags.keySet(), newGFlags.keySet());

    for (String gFlagName : modifiedGFlags) {
      String oldGFlagValue = oldGFlags.getOrDefault(gFlagName, null);
      String newGFlagValue = newGFlags.getOrDefault(gFlagName, null);
      if (oldGFlagValue == null || !oldGFlagValue.equals(newGFlagValue)) {
        String defaultGFlagValue = getGFlagDefaultValue(softwareVersion, serverType, gFlagName);
        tEntry = new GFlagDiffEntry(gFlagName, oldGFlagValue, newGFlagValue, defaultGFlagValue);
        gFlagChanges.add(tEntry);
      }
    }

    return gFlagChanges;
  }

  public String getGFlagDefaultValue(String softwareVersion, String serverType, String gFlagName) {
    GFlagDetails defaultGFlag;
    String defaultGFlagValue;
    try {
      defaultGFlag =
          gFlagsValidationHandler.getGFlagsMetadata(softwareVersion, serverType, gFlagName);
    } catch (IOException | PlatformServiceException e) {
      defaultGFlag = null;
    }
    defaultGFlagValue = (defaultGFlag == null) ? null : defaultGFlag.defaultValue;
    return defaultGFlagValue;
  }

  public UUID rotateCerts(CertsRotateParams requestParams, Customer customer, Universe universe) {
    log.debug(
        "rotateCerts called with rootCA: {}",
        (requestParams.rootCA != null) ? requestParams.rootCA.toString() : "NULL");
    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;
    if (universe.isYbcEnabled()) {
      requestParams.installYbc = true;
      requestParams.enableYbc = true;
      requestParams.ybcSoftwareVersion = universe.getUniverseDetails().ybcSoftwareVersion;
      requestParams.ybcInstalled = true;
    }

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
    // This is there only for legacy support, no need if rootCA and clientRootCA are different.
    if (userIntent.enableClientToNodeEncrypt && requestParams.rootAndClientRootCASame) {
      CertificateInfo rootCert = CertificateInfo.get(requestParams.rootCA);
      if (rootCert.certType == CertConfigType.SelfSigned
          || rootCert.certType == CertConfigType.HashicorpVault) {
        CertificateHelper.createClientCertificate(
            runtimeConfigFactory.staticApplicationConf(), customer.uuid, requestParams.rootCA);
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

  void mergeResizeNodeParamsWithIntent(ResizeNodeParams requestParams, Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;
    requestParams.rootCA = universeDetails.rootCA;
    requestParams.clientRootCA = universeDetails.clientRootCA;
    // Merging existent intent with that of from request.
    for (UniverseDefinitionTaskParams.Cluster requestCluster : requestParams.clusters) {
      UniverseDefinitionTaskParams.Cluster cluster =
          universeDetails.getClusterByUuid(requestCluster.uuid);
      UserIntent requestIntent = requestCluster.userIntent;
      requestCluster.userIntent = cluster.userIntent;
      if (requestIntent.instanceType != null) {
        requestCluster.userIntent.instanceType = requestIntent.instanceType;
      }
      if (requestIntent.deviceInfo != null && requestIntent.deviceInfo.volumeSize != null) {
        requestCluster.userIntent.deviceInfo.volumeSize = requestIntent.deviceInfo.volumeSize;
      }
    }
  }

  public UUID resizeNode(ResizeNodeParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    mergeResizeNodeParamsWithIntent(requestParams, universe);

    return submitUpgradeTask(
        TaskType.ResizeNode, CustomerTask.TaskType.ResizeNode, requestParams, customer, universe);
  }

  public UUID thirdpartySoftwareUpgrade(
      ThirdpartySoftwareUpgradeParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    return submitUpgradeTask(
        TaskType.ThirdpartySoftwareUpgrade,
        CustomerTask.TaskType.ThirdpartySoftwareUpgrade,
        requestParams,
        customer,
        universe);
  }

  // Enable/Disable TLS on Cluster
  public UUID toggleTls(TlsToggleParams requestParams, Customer customer, Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;

    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;
    if (universe.isYbcEnabled()) {
      requestParams.installYbc = true;
      requestParams.enableYbc = true;
      requestParams.ybcSoftwareVersion = universe.getUniverseDetails().ybcSoftwareVersion;
      requestParams.ybcInstalled = true;
    }
    if (requestParams.rootAndClientRootCASame == null) {
      requestParams.rootAndClientRootCASame = universeDetails.rootAndClientRootCASame;
    }
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
              CertificateHelper.createRootCA(
                  runtimeConfigFactory.staticApplicationConf(),
                  universeDetails.nodePrefix,
                  customer.uuid);
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
      if (universeDetails.clientRootCA == null) {
        if (requestParams.clientRootCA == null) {
          if (requestParams.rootCA != null && requestParams.rootAndClientRootCASame) {
            // Setting ClientRootCA to RootCA in case rootAndClientRootCA is true
            requestParams.clientRootCA = requestParams.rootCA;
          } else {
            // Create self-signed clientRootCA in case it is not provided by the user
            // and rootCA and clientRootCA needs to be different
            requestParams.clientRootCA =
                CertificateHelper.createClientRootCA(
                    runtimeConfigFactory.staticApplicationConf(),
                    universeDetails.nodePrefix,
                    customer.uuid);
          }
        }
      } else {
        requestParams.clientRootCA = universeDetails.clientRootCA;
      }

      // Setting rootCA to ClientRootCA in case node to node encryption is disabled.
      // This is necessary to set to ensure backward compatibility as existing parts of
      // codebase uses rootCA for Client to Node Encryption
      if (requestParams.rootCA == null && requestParams.rootAndClientRootCASame) {
        requestParams.rootCA = requestParams.clientRootCA;
      }

      // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
      // This is there only for legacy support, no need if rootCA and clientRootCA are different.
      if (requestParams.rootAndClientRootCASame) {
        CertificateInfo cert = CertificateInfo.get(requestParams.rootCA);
        if (cert.certType == CertConfigType.SelfSigned
            || cert.certType == CertConfigType.HashicorpVault) {
          CertificateHelper.createClientCertificate(
              runtimeConfigFactory.staticApplicationConf(), customer.uuid, requestParams.rootCA);
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
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    return submitUpgradeTask(
        TaskType.VMImageUpgrade,
        CustomerTask.TaskType.VMImageUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID upgradeSystemd(
      SystemdUpgradeParams requestParams, Customer customer, Universe universe) {

    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    return submitUpgradeTask(
        TaskType.SystemdUpgrade,
        CustomerTask.TaskType.SystemdUpgrade,
        requestParams,
        customer,
        universe);
  }

  public UUID rebootUniverse(
      UpgradeTaskParams requestParams, Customer customer, Universe universe) {
    requestParams.verifyParams(universe);
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    return submitUpgradeTask(
        TaskType.RebootUniverse,
        CustomerTask.TaskType.RebootUniverse,
        requestParams,
        customer,
        universe);
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
        universe.universeUUID,
        universe.name,
        taskUUID);

    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        customerTaskType,
        universe.name,
        customTaskName);
    log.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.universeUUID,
        universe.name);
    return taskUUID;
  }

  private Map<String, String> trimFlags(Map<String, String> data) {
    Map<String, String> trimData = new HashMap<>();
    for (Map.Entry<String, String> intent : data.entrySet()) {
      String key = intent.getKey();
      String value = intent.getValue();
      trimData.put(key.trim(), value.trim());
    }
    return trimData;
  }

  private void checkHelmChartExists(String ybSoftwareVersion) {
    try {
      kubernetesManagerFactory.getManager().getHelmPackagePath(ybSoftwareVersion);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(Status.BAD_REQUEST, e.getMessage());
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
