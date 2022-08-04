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

import static com.yugabyte.yw.forms.PlatformResults.YBPSuccess.empty;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.forms.AlertConfigFormData;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.ToggleTlsParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;

import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Universe management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class UniverseActionsController extends AuthenticatedController {
  @Inject private UniverseActionsHandler universeActionsHandler;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @ApiOperation(
      value = "Configure alerts for a universe",
      nickname = "configureUniverseAlerts",
      response = YBPSuccess.class)
  public Result configureAlerts(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    universeActionsHandler.configureAlerts(
        universe, formFactory.getFormDataOrBadRequest(AlertConfigFormData.class));
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.ConfigUniverseAlert,
            request().body().asJson());
    return empty();
  }

  @ApiOperation(value = "Pause a universe", nickname = "pauseUniverse", response = YBPTask.class)
  public Result pause(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    // Check if the universe is of type kubernetes, if yes throw an exception
    Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
    List<Cluster> readOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
    CloudType cloudType = cluster.userIntent.providerType;
    Boolean isKubernetesCluster = (cloudType == CloudType.kubernetes);
    for (Cluster readCluster : readOnlyClusters) {
      cloudType = readCluster.userIntent.providerType;
      isKubernetesCluster = isKubernetesCluster || cloudType == CloudType.kubernetes;
    }

    if (isKubernetesCluster) {
      String msg =
          String.format("Pause task is not supported for Kubernetes universe - %s", universe.name);
      log.error(msg);
      throw new IllegalArgumentException(msg);
    }

    UUID taskUUID = universeActionsHandler.pause(customer, universe);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.Pause,
            taskUUID);
    return new YBPTask(taskUUID, universe.universeUUID).asResult();
  }

  @ApiOperation(
      value = "Resume a paused universe",
      nickname = "resumeUniverse",
      response = YBPTask.class)
  public Result resume(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeActionsHandler.resume(customer, universe);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.Resume,
            taskUUID);
    return new YBPTask(taskUUID, universe.universeUUID).asResult();
  }

  @ApiOperation(
      value = "Set a universe's key",
      nickname = "setUniverseKey",
      response = UniverseResp.class)
  public Result setUniverseKey(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    log.info("Updating universe key {} for {}.", universe.universeUUID, customer.uuid);
    // Get the user submitted form data.

    EncryptionAtRestKeyParams taskParams =
        EncryptionAtRestKeyParams.bindFromFormData(universe.universeUUID, request());

    UUID taskUUID = universeActionsHandler.setUniverseKey(customer, universe, taskParams);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.SetUniverseKey,
            taskUUID);
    UniverseResp resp =
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf());
    return PlatformResults.withData(resp);
  }

  @Deprecated
  @ApiOperation(
      value = "Toggle a universe's TLS state",
      notes =
          "Enable or disable node-to-node and client-to-node encryption. "
              + "Supports rolling and non-rolling universe upgrades.",
      nickname = "toggleUniverseTLS",
      response = UniverseResp.class)
  public Result toggleTls(UUID customerUuid, UUID universeUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUuid, customer);
    ObjectNode formData = (ObjectNode) request().body().asJson();
    ToggleTlsParams requestParams = ToggleTlsParams.bindFromFormData(formData);
    UUID taskUUID = universeActionsHandler.toggleTls(customer, universe, requestParams);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUuid.toString(),
            Audit.ActionType.ToggleTls,
            Json.toJson(formData),
            taskUUID);
    return PlatformResults.withData(
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf()));
  }

  /**
   * Mark whether the universe needs to be backed up or not.
   *
   * @return Result
   */
  @ApiOperation(
      value = "Set a universe's backup flag",
      nickname = "setUniverseBackupFlag",
      tags = {"Universe management", "Backups"},
      response = YBPSuccess.class)
  public Result setBackupFlag(UUID customerUUID, UUID universeUUID, Boolean markActive) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universeActionsHandler.setBackupFlag(universe, markActive);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.SetBackupFlag);
    return empty();
  }

  /**
   * Mark whether the universe has been made helm compatible.
   *
   * @return Result
   */
  @ApiOperation(
      value = "Flag a universe as Helm 3-compatible",
      nickname = "setUniverseHelm3Compatible",
      response = YBPSuccess.class)
  public Result setHelm3Compatible(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    universeActionsHandler.setHelm3Compatible(universe);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.SetHelm3Compatible);
    return empty();
  }

  /**
   * API that sets universe version number to -1
   *
   * @return result of settings universe version to -1 (either success if universe exists else
   *     failure
   */
  @ApiOperation(
      value = "Reset universe version",
      nickname = "resetUniverseVersion",
      response = YBPSuccess.class)
  public Result resetVersion(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.ResetUniverseVersion);
    universe.resetVersion();
    return empty();
  }
}
