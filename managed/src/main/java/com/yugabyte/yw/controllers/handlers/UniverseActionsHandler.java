/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.PauseUniverse;
import com.yugabyte.yw.commissioner.tasks.ResumeUniverse;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.AlertConfigFormData;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.ToggleTlsParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Http;

public class UniverseActionsHandler {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseActionsHandler.class);

  private final Commissioner commissioner;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public UniverseActionsHandler(
      Commissioner commissioner, RuntimeConfigFactory runtimeConfigFactory) {
    this.commissioner = commissioner;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public void setBackupFlag(Universe universe, Boolean value) {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, value.toString());
    universe.updateConfig(config);
  }

  public UUID setUniverseKey(
      Customer customer, Universe universe, EncryptionAtRestKeyParams taskParams) {
    try {
      TaskType taskType = TaskType.SetUniverseKey;
      taskParams.expectedUniverseVersion = universe.version;
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info(
          "Submitted set universe key for {}:{}, task uuid = {}.",
          universe.universeUUID,
          universe.name,
          taskUUID);

      CustomerTask.TaskType customerTaskType = null;
      switch (taskParams.encryptionAtRestConfig.opType) {
        case ENABLE:
          if (universe.getUniverseDetails().encryptionAtRestConfig.encryptionAtRestEnabled) {
            customerTaskType = CustomerTask.TaskType.RotateEncryptionKey;
          } else {
            customerTaskType = CustomerTask.TaskType.EnableEncryptionAtRest;
          }
          break;
        case DISABLE:
          customerTaskType = CustomerTask.TaskType.DisableEncryptionAtRest;
          break;
        default:
        case UNDEFINED:
          break;
      }

      // Add this task uuid to the user universe.
      CustomerTask.create(
          customer,
          universe.universeUUID,
          taskUUID,
          CustomerTask.TargetType.Universe,
          customerTaskType,
          universe.name);
      LOG.info(
          "Saved task uuid "
              + taskUUID
              + " in customer tasks table for universe "
              + universe.universeUUID
              + ":"
              + universe.name);
      return taskUUID;
    } catch (RuntimeException e) {
      String errMsg =
          String.format(
              "Error occurred attempting to %s the universe encryption key",
              taskParams.encryptionAtRestConfig.opType.name());
      LOG.error(errMsg, e);
      throw new PlatformServiceException(Http.Status.BAD_REQUEST, errMsg);
    }
  }

  public UUID toggleTls(Customer customer, Universe universe, ToggleTlsParams requestParams) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universeDetails.getPrimaryCluster().userIntent;

    LOG.info(
        "Toggle TLS for universe {} [ {} ] customer {}.",
        universe.name,
        universe.universeUUID,
        customer.uuid);

    YBPError error = requestParams.verifyParams(universeDetails);
    if (error != null) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, error.error + " - for universe: " + universe.universeUUID);
    }

    if (!universeDetails.isUniverseEditable()) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Universe UUID " + universe.universeUUID + " cannot be edited.");
    }

    if (universe.nodesInTransit()) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "Cannot perform a toggle TLS operation on universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    if (!CertificateInfo.isCertificateValid(requestParams.rootCA)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          String.format(
              "The certificate %s needs info. Update the cert and retry.",
              CertificateInfo.get(requestParams.rootCA).label));
    }

    if (requestParams.rootCA != null
        && CertificateInfo.get(requestParams.rootCA).certType == CertConfigType.CustomServerCert) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "CustomServerCert are only supported for Client to Server Communication.");
    }

    if (requestParams.rootCA != null
        && CertificateInfo.get(requestParams.rootCA).certType == CertConfigType.CustomCertHostPath
        && !userIntent.providerType.equals(Common.CloudType.onprem)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "CustomCertHostPath certificates are only supported for on-prem providers.");
    }

    if (requestParams.clientRootCA != null
        && CertificateInfo.get(requestParams.clientRootCA).certType
            == CertConfigType.CustomCertHostPath
        && !userIntent.providerType.equals(Common.CloudType.onprem)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "CustomCertHostPath certificates are only supported for on-prem providers.");
    }

    if (requestParams.rootAndClientRootCASame != null
        && requestParams.rootAndClientRootCASame
        && requestParams.enableNodeToNodeEncrypt
        && requestParams.enableClientToNodeEncrypt
        && requestParams.rootCA != null
        && requestParams.clientRootCA != null
        && requestParams.rootCA != requestParams.clientRootCA) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "RootCA and ClientRootCA cannot be different when rootAndClientRootCASame is true.");
    }

    TaskType taskType = TaskType.UpgradeUniverse;
    UpgradeParams taskParams = new UpgradeParams();
    taskParams.taskType = UpgradeTaskType.ToggleTls;
    taskParams.upgradeOption = requestParams.upgradeOption;
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = -1;
    taskParams.enableNodeToNodeEncrypt = requestParams.enableNodeToNodeEncrypt;
    taskParams.enableClientToNodeEncrypt = requestParams.enableClientToNodeEncrypt;
    taskParams.rootAndClientRootCASame =
        requestParams.rootAndClientRootCASame != null
            ? requestParams.rootAndClientRootCASame
            : universeDetails.rootAndClientRootCASame;
    taskParams.allowInsecure =
        !(requestParams.enableNodeToNodeEncrypt || requestParams.enableClientToNodeEncrypt);

    if (userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Kubernetes Upgrade is not supported.");
    }

    if (requestParams.enableNodeToNodeEncrypt) {
      // Setting the rootCA to the already existing rootCA as we do not
      // support root certificate rotation through ToggleTLS.
      // There is a check for different new and existing root cert already.
      taskParams.rootCA = universeDetails.rootCA;
      if (taskParams.rootCA == null) {
        // create self signed rootCA in case it is not provided by the user
        LOG.info("creating selfsigned CA for {}", universeDetails.universeUUID.toString());
        taskParams.rootCA =
            requestParams.rootCA != null
                ? requestParams.rootCA
                : CertificateHelper.createRootCA(
                    runtimeConfigFactory.staticApplicationConf(),
                    universeDetails.nodePrefix,
                    customer.uuid);
      }
    }

    if (requestParams.enableClientToNodeEncrypt) {
      // Setting the ClientRootCA to the already existing clientRootCA as we do not
      // support root certificate rotation through ToggleTLS.
      // There is a check for different new and existing root cert already.
      taskParams.clientRootCA = universeDetails.clientRootCA;
      if (taskParams.clientRootCA == null) {
        if (requestParams.clientRootCA == null) {
          if (taskParams.rootCA != null && taskParams.rootAndClientRootCASame) {
            // Setting ClientRootCA to RootCA incase rootAndClientRootCA is true
            taskParams.clientRootCA = taskParams.rootCA;
          } else {
            // create self signed clientRootCA in case it is not provided by the user
            // and rootCA and clientRootCA needs to be different
            taskParams.clientRootCA =
                CertificateHelper.createClientRootCA(
                    runtimeConfigFactory.staticApplicationConf(),
                    universeDetails.nodePrefix,
                    customer.uuid);
          }
        } else {
          // Set the ClientRootCA to the user provided ClientRootCA if it exists
          taskParams.clientRootCA = requestParams.clientRootCA;
        }
      }

      // Setting rootCA to ClientRootCA in case node to node encryption is disabled.
      // This is necessary to set to ensure backward compatibity as existing parts of
      // codebase uses rootCA for Client to Node Encryption
      if (taskParams.rootCA == null && taskParams.rootAndClientRootCASame) {
        taskParams.rootCA = taskParams.clientRootCA;
      }

      // If client encryption is enabled, generate the client cert file for each node.
      CertificateInfo cert = CertificateInfo.get(taskParams.rootCA);
      if (taskParams.rootAndClientRootCASame) {
        if (cert.certType == CertConfigType.SelfSigned
            || cert.certType == CertConfigType.HashicorpVault) {
          CertificateHelper.createClientCertificate(
              runtimeConfigFactory.staticApplicationConf(), customer.uuid, taskParams.clientRootCA);
        }
      }
    }

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted toggle tls for {} : {}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        taskUUID);

    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ToggleTls,
        universe.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.universeUUID,
        universe.name);
    return taskUUID;
  }

  public void setHelm3Compatible(Universe universe) {
    // Check if the provider is k8s and that we haven't already marked this universe
    // as helm compatible.
    Map<String, String> universeConfig = universe.getConfig();
    if (universeConfig.containsKey(Universe.HELM2_LEGACY)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Universe was already marked as helm3 compatible.");
    }
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    if (!primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Only applicable for k8s universes.");
    }

    Map<String, String> config = new HashMap<>();
    config.put(Universe.HELM2_LEGACY, Universe.HelmLegacy.V2TO3.toString());
    universe.updateConfig(config);
  }

  public void configureAlerts(Universe universe, Form<AlertConfigFormData> formData) {
    Map<String, String> config = new HashMap<>();

    AlertConfigFormData alertConfig = formData.get();
    long disabledUntilSecs = 0;
    if (alertConfig.disabled) {
      if (null == alertConfig.disablePeriodSecs) {
        disabledUntilSecs = Long.MAX_VALUE;
      } else {
        disabledUntilSecs = (System.currentTimeMillis() / 1000) + alertConfig.disablePeriodSecs;
      }
      LOG.info(
          String.format(
              "Will disable alerts for universe %s until unix time %d [ %s ].",
              universe.universeUUID, disabledUntilSecs, Util.unixTimeToString(disabledUntilSecs)));
    } else {
      LOG.info(
          String.format(
              "Will enable alerts for universe %s [unix time  = %d].",
              universe.universeUUID, disabledUntilSecs));
    }
    config.put(Universe.DISABLE_ALERTS_UNTIL, Long.toString(disabledUntilSecs));
    universe.updateConfig(config);
  }

  public UUID pause(Customer customer, Universe universe) {
    LOG.info(
        "Pause universe, customer uuid: {}, universe: {} [ {} ] ",
        customer.uuid,
        universe.name,
        universe.universeUUID);

    // Create the Commissioner task to pause the universe.
    PauseUniverse.Params taskParams = new PauseUniverse.Params();
    taskParams.universeUUID = universe.universeUUID;
    // There is no staleness of a pause request. Perform it even if the universe has changed.
    taskParams.expectedUniverseVersion = -1;
    taskParams.customerUUID = customer.uuid;
    // Submit the task to pause the universe.
    TaskType taskType = TaskType.PauseUniverse;

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info("Submitted pause universe for " + universe.universeUUID + ", task uuid = " + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Pause,
        universe.name);

    LOG.info("Paused universe " + universe.universeUUID + " for customer [" + customer.name + "]");
    return taskUUID;
  }

  public UUID resume(Customer customer, Universe universe) {
    LOG.info(
        "Resume universe, customer uuid: {}, universe: {} [ {} ] ",
        customer.uuid,
        universe.name,
        universe.universeUUID);

    // Create the Commissioner task to resume the universe.
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.universeUUID = universe.universeUUID;
    // There is no staleness of a resume request. Perform it even if the universe has changed.
    taskParams.expectedUniverseVersion = -1;
    taskParams.customerUUID = customer.uuid;
    taskParams.clusters = universe.getUniverseDetails().clusters;

    // Submit the task to resume the universe.
    TaskType taskType = TaskType.ResumeUniverse;

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted resume universe for " + universe.universeUUID + ", task uuid = " + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Resume,
        universe.name);

    LOG.info("Resumed universe " + universe.universeUUID + " for customer [" + customer.name + "]");
    return taskUUID;
  }
}
