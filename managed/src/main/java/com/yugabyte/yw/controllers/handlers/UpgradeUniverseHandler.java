// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

@Slf4j
public class UpgradeUniverseHandler {

  private final Commissioner commissioner;
  private final KubernetesManager kubernetesManager;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public UpgradeUniverseHandler(
      Commissioner commissioner,
      KubernetesManager kubernetesManager,
      RuntimeConfigFactory runtimeConfigFactory) {
    this.commissioner = commissioner;
    this.kubernetesManager = kubernetesManager;
    this.runtimeConfigFactory = runtimeConfigFactory;
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

    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    if (userIntent.providerType.equals(CloudType.kubernetes)) {
      checkHelmChartExists(requestParams.ybSoftwareVersion);
    }

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

  public UUID rotateCerts(CertsRotateParams requestParams, Customer customer, Universe universe) {
    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
    // This is there only for legacy support, no need if rootCA and clientRootCA are different.
    if (userIntent.enableClientToNodeEncrypt && requestParams.rootAndClientRootCASame) {
      CertificateInfo rootCert = CertificateInfo.get(requestParams.rootCA);
      if (rootCert.certType == CertificateInfo.Type.SelfSigned) {
        CertificateHelper.createClientCertificate(
            requestParams.rootCA,
            String.format(
                CertificateHelper.CERT_PATH,
                runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"),
                customer.uuid.toString(),
                requestParams.rootCA.toString()),
            CertificateHelper.DEFAULT_CLIENT,
            null,
            null);
      }
    }

    return submitUpgradeTask(
        TaskType.CertsRotate, CustomerTask.TaskType.CertsRotate, requestParams, customer, universe);
  }

  public UUID toggleTls(TlsToggleParams requestParams, Customer customer, Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;

    // Verify request params
    requestParams.verifyParams(universe);
    // Update request params with additional metadata for upgrade task
    requestParams.universeUUID = universe.universeUUID;
    requestParams.expectedUniverseVersion = universe.version;
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
                  universeDetails.nodePrefix,
                  customer.uuid,
                  runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"));
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
                    universeDetails.nodePrefix,
                    customer.uuid,
                    runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"));
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
        if (cert.certType == CertificateInfo.Type.SelfSigned) {
          CertificateHelper.createClientCertificate(
              requestParams.rootCA,
              String.format(
                  CertificateHelper.CERT_PATH,
                  runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"),
                  customer.uuid.toString(),
                  requestParams.rootCA.toString()),
              CertificateHelper.DEFAULT_CLIENT,
              null,
              null);
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
      kubernetesManager.getHelmPackagePath(ybSoftwareVersion);
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
