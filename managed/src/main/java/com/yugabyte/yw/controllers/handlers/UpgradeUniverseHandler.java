// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
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
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

@Slf4j
public class UpgradeUniverseHandler {

  @Inject Commissioner commissioner;

  @Inject KubernetesManager kubernetesManager;

  @Inject Config appConfig;

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
    requestParams.allowInsecure =
        !(requestParams.enableNodeToNodeEncrypt || requestParams.enableClientToNodeEncrypt);
    if (universeDetails.rootCA == null) {
      if (requestParams.rootCA == null) {
        // If certificate is not present and user has not provided any then create new root cert
        requestParams.rootCA =
            CertificateHelper.createRootCA(
                universeDetails.nodePrefix, customer.uuid, appConfig.getString("yb.storage.path"));
      }
    } else {
      // If certificate already present then use the same as upgrade cannot rotate certs
      requestParams.rootCA = universeDetails.rootCA;
    }

    // Create client certificate if not exists
    if (!userIntent.enableClientToNodeEncrypt && requestParams.enableClientToNodeEncrypt) {
      CertificateInfo cert = CertificateInfo.get(requestParams.rootCA);
      if (cert.certType == CertificateInfo.Type.SelfSigned) {
        CertificateHelper.createClientCertificate(
            requestParams.rootCA,
            String.format(
                CertificateHelper.CERT_PATH,
                appConfig.getString("yb.storage.path"),
                customer.uuid.toString(),
                requestParams.rootCA.toString()),
            CertificateHelper.DEFAULT_CLIENT,
            null,
            null);
      }
    }

    return submitUpgradeTask(
        TaskType.TlsToggle, CustomerTask.TaskType.TlsToggle, requestParams, customer, universe);
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

  private UUID submitUpgradeTask(
      TaskType taskType,
      CustomerTask.TaskType customerTaskType,
      UpgradeTaskParams upgradeTaskParams,
      Customer customer,
      Universe universe) {
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
        universe.name);
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
      throw new YWServiceException(Status.BAD_REQUEST, e.getMessage());
    }
  }
}
