/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.PauseUniverse;
import com.yugabyte.yw.commissioner.tasks.ResumeUniverse;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Http;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class UniverseActionsHandler {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseActionsHandler.class);

  @Inject Commissioner commissioner;
  @Inject play.Configuration appConfig;

  void setBackupFlag(Universe universe, Boolean value) {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, value.toString());
    universe.updateConfig(config);
  }

  UUID setUniverseKey(Customer customer, Universe universe, EncryptionAtRestKeyParams taskParams) {
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
      throw new YWServiceException(Http.Status.BAD_REQUEST, errMsg);
    }
  }

  UUID toggleTls(Customer customer, Universe universe, ToggleTlsParams requestParams) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universeDetails.getPrimaryCluster().userIntent;

    LOG.info(
        "Toggle TLS for universe {} [ {} ] customer {}.",
        universe.name,
        universe.universeUUID,
        customer.uuid);

    YWResults.YWError error = requestParams.verifyParams(universeDetails);
    if (error != null) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, error.error + " - for universe: " + universe.universeUUID);
    }

    if (!universeDetails.isUniverseEditable()) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, "Universe UUID " + universe.universeUUID + " cannot be edited.");
    }

    if (universe.nodesInTransit()) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST,
          "Cannot perform a toggle TLS operation on universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    if (!CertificateInfo.isCertificateValid(requestParams.rootCA)) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST,
          String.format(
              "The certificate %s needs info. Update the cert and retry.",
              CertificateInfo.get(requestParams.rootCA).label));
    }

    if (requestParams.rootCA != null
        && CertificateInfo.get(requestParams.rootCA).certType
            == CertificateInfo.Type.CustomCertHostPath
        && !userIntent.providerType.equals(Common.CloudType.onprem)) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, "Custom certificates are only supported for on-prem providers.");
    }

    TaskType taskType = TaskType.UpgradeUniverse;
    UpgradeParams taskParams = new UpgradeParams();
    taskParams.taskType = UpgradeUniverse.UpgradeTaskType.ToggleTls;
    taskParams.upgradeOption = requestParams.upgradeOption;
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = -1;
    taskParams.enableNodeToNodeEncrypt = requestParams.enableNodeToNodeEncrypt;
    taskParams.enableClientToNodeEncrypt = requestParams.enableClientToNodeEncrypt;
    taskParams.allowInsecure =
        !(requestParams.enableNodeToNodeEncrypt || requestParams.enableClientToNodeEncrypt);

    if (userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      throw new YWServiceException(Http.Status.BAD_REQUEST, "Kubernetes Upgrade is not supported.");
    }

    if (!universeDetails.rootAndClientRootCASame
        || (universeDetails.rootCA != universeDetails.clientRootCA)) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, "RootCA and ClientRootCA cannot be different for Upgrade.");
    }

    // Create root certificate if not exist
    taskParams.rootCA = universeDetails.rootCA;
    if (taskParams.rootCA == null) {
      taskParams.rootCA =
          requestParams.rootCA != null
              ? requestParams.rootCA
              : CertificateHelper.createRootCA(
                  universeDetails.nodePrefix,
                  customer.uuid,
                  appConfig.getString("yb.storage.path"));
    }

    // Create client certificate if not exists
    if (!userIntent.enableClientToNodeEncrypt && requestParams.enableClientToNodeEncrypt) {
      CertificateInfo cert = CertificateInfo.get(taskParams.rootCA);
      if (cert.certType == CertificateInfo.Type.SelfSigned) {
        CertificateHelper.createClientCertificate(
            taskParams.rootCA,
            String.format(
                CertificateHelper.CERT_PATH,
                appConfig.getString("yb.storage.path"),
                customer.uuid.toString(),
                taskParams.rootCA.toString()),
            CertificateHelper.DEFAULT_CLIENT,
            null,
            null);
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

  void setHelm3Compatible(Universe universe) {
    // Check if the provider is k8s and that we haven't already marked this universe
    // as helm compatible.
    Map<String, String> universeConfig = universe.getConfig();
    if (universeConfig.containsKey(Universe.HELM2_LEGACY)) {
      throw new YWServiceException(
          Http.Status.BAD_REQUEST, "Universe was already marked as helm3 compatible.");
    }
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    if (!primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      throw new YWServiceException(Http.Status.BAD_REQUEST, "Only applicable for k8s universes.");
    }

    Map<String, String> config = new HashMap<>();
    config.put(Universe.HELM2_LEGACY, Universe.HelmLegacy.V2TO3.toString());
    universe.updateConfig(config);
  }

  void configureAlerts(Universe universe, Form<AlertConfigFormData> formData) {
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

  UUID pause(Customer customer, Universe universe) {
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

  UUID resume(Customer customer, Universe universe) {
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
