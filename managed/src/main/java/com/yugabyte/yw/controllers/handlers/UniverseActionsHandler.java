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

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.PauseUniverse;
import com.yugabyte.yw.commissioner.tasks.ResumeUniverse;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.AlertConfigFormData;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;

@Singleton
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
    universe.save();
  }

  public UUID setUniverseKey(
      Customer customer, Universe universe, EncryptionAtRestKeyParams taskParams) {
    try {
      TaskType taskType = TaskType.SetUniverseKey;
      taskParams.expectedUniverseVersion = universe.getVersion();
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info(
          "Submitted set universe key for {}:{}, task uuid = {}.",
          universe.getUniverseUUID(),
          universe.getName(),
          taskUUID);

      CustomerTask.TaskType customerTaskType = null;
      CustomerTask.TargetType customerTargetType = CustomerTask.TargetType.Universe;
      KmsHistory activeKmsHistory = EncryptionAtRestUtil.getActiveKey(universe.getUniverseUUID());
      switch (taskParams.encryptionAtRestConfig.opType) {
        case ENABLE:
          if (universe.getUniverseDetails().encryptionAtRestConfig.encryptionAtRestEnabled) {
            customerTaskType = CustomerTask.TaskType.RotateEncryptionKey;
            if (activeKmsHistory != null
                && !activeKmsHistory
                    .getConfigUuid()
                    .equals(taskParams.encryptionAtRestConfig.kmsConfigUUID)) {
              // Master key rotation case when given config UUID differs from active config UUID.
              customerTargetType = CustomerTask.TargetType.MasterKey;
            } else {
              // Universe key rotation case when given config UUID matches active config UUID.
              customerTargetType = CustomerTask.TargetType.UniverseKey;
            }
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
          universe.getUniverseUUID(),
          taskUUID,
          customerTargetType,
          customerTaskType,
          universe.getName());
      LOG.info(
          "Saved task uuid "
              + taskUUID
              + " in customer tasks table for universe "
              + universe.getUniverseUUID()
              + ":"
              + universe.getName());
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
    universe.save();
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
              universe.getUniverseUUID(),
              disabledUntilSecs,
              Util.unixTimeToString(disabledUntilSecs)));
    } else {
      LOG.info(
          String.format(
              "Will enable alerts for universe %s [unix time  = %d].",
              universe.getUniverseUUID(), disabledUntilSecs));
    }
    config.put(Universe.DISABLE_ALERTS_UNTIL, Long.toString(disabledUntilSecs));
    universe.updateConfig(config);
    universe.save();
  }

  public UUID pause(Customer customer, Universe universe) {
    LOG.info(
        "Pause universe, customer uuid: {}, universe: {} [ {} ] ",
        customer.getUuid(),
        universe.getName(),
        universe.getUniverseUUID());

    // Create the Commissioner task to pause the universe.
    PauseUniverse.Params taskParams = new PauseUniverse.Params();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    // There is no staleness of a pause request. Perform it even if the universe has changed.
    taskParams.expectedUniverseVersion = -1;
    taskParams.customerUUID = customer.getUuid();
    // Submit the task to pause the universe.
    TaskType taskType = TaskType.PauseUniverse;

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted pause universe for " + universe.getUniverseUUID() + ", task uuid = " + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Pause,
        universe.getName());

    LOG.info(
        "Paused universe "
            + universe.getUniverseUUID()
            + " for customer ["
            + customer.getName()
            + "]");
    return taskUUID;
  }

  public UUID resume(Customer customer, Universe universe) throws IOException {
    LOG.info(
        "Resume universe, customer uuid: {}, universe: {} [ {} ] ",
        customer.getUuid(),
        universe.getName(),
        universe.getUniverseUUID());

    // Create the Commissioner task to resume the universe.
    // TODO: this is better done using copy constructors
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    ResumeUniverse.Params taskParams =
        mapper.readValue(
            mapper.writeValueAsString(universe.getUniverseDetails()), ResumeUniverse.Params.class);
    // There is no staleness of a resume request. Perform it even if the universe has changed.
    taskParams.expectedUniverseVersion = -1;
    taskParams.customerUUID = customer.getUuid();

    // Submit the task to resume the universe.
    TaskType taskType = TaskType.ResumeUniverse;

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted resume universe for "
            + universe.getUniverseUUID()
            + ", task uuid = "
            + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Resume,
        universe.getName());

    LOG.info(
        "Resumed universe "
            + universe.getUniverseUUID()
            + " for customer ["
            + customer.getName()
            + "]");
    return taskUUID;
  }

  public UUID updateLoadBalancerConfig(
      Customer customer, Universe universe, UniverseDefinitionTaskParams taskParams) {
    if (!taskParams.getUniverseUUID().equals(universe.getUniverseUUID())) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "Invalid Universe UUID in json: "
              + taskParams.getUniverseUUID().toString()
              + " Expected UUID: "
              + universe.getUniverseUUID().toString());
    }

    LOG.info(
        "Update load balancer config, universe: {} [ {} ] ",
        universe.getName(),
        universe.getUniverseUUID());
    // Set existing LB config
    taskParams.setExistingLBs(universe.getUniverseDetails().clusters);
    // Task to update LB config
    TaskType taskType = TaskType.UpdateLoadBalancerConfig;
    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted update load balancer config for {} : {}, task uuid = {}.",
        universe.getUniverseUUID(),
        universe.getName(),
        taskUUID);

    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.UpdateLoadBalancerConfig,
        universe.getName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.getUniverseUUID(),
        universe.getName());
    return taskUUID;
  }
}
