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

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

import static com.yugabyte.yw.forms.YWResults.YWSuccess.empty;

@Api(
    value = "Universe Actions",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class UniverseActionsController extends AuthenticatedController {
  @Inject private UniverseActionsHandler universeActionsHandler;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @ApiOperation(value = "Configure Alerts for a universe", response = YWResults.YWSuccess.class)
  public Result configureAlerts(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    universeActionsHandler.configureAlerts(
        universe, formFactory.getFormDataOrBadRequest(AlertConfigFormData.class));
    // TODO Audit ??
    return empty();
  }

  @ApiOperation(value = "Pause the universe", response = YWResults.YWTask.class)
  public Result pause(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeActionsHandler.pause(customer, universe);
    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }

  @ApiOperation(value = "Resume the universe", response = YWResults.YWTask.class)
  public Result resume(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeActionsHandler.resume(customer, universe);

    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }

  @ApiOperation(value = "setUniverseKey", response = UniverseResp.class)
  public Result setUniverseKey(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    log.info("Updating universe key {} for {}.", universe.universeUUID, customer.uuid);
    // Get the user submitted form data.

    EncryptionAtRestKeyParams taskParams =
        EncryptionAtRestKeyParams.bindFromFormData(universe.universeUUID, request());

    UUID taskUUID = universeActionsHandler.setUniverseKey(customer, universe, taskParams);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    UniverseResp resp =
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf());
    return YWResults.withData(resp);
  }

  @ApiOperation(
      value = "API that toggles TLS state of the universe.",
      notes =
          "Can enable/disable node to node and client to node encryption. "
              + "Supports rolling and non-rolling upgrade of the universe.",
      response = UniverseResp.class)
  public Result toggleTls(UUID customerUuid, UUID universeUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUuid, customer);
    ObjectNode formData = (ObjectNode) request().body().asJson();
    ToggleTlsParams requestParams = ToggleTlsParams.bindFromFormData(formData);

    UUID taskUUID = universeActionsHandler.toggleTls(customer, universe, requestParams);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData), taskUUID);
    return YWResults.withData(
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf()));
  }

  /**
   * Mark whether the universe needs to be backed up or not.
   *
   * @return Result
   */
  @ApiOperation(value = "Set backup Flag for a universe", response = YWResults.YWSuccess.class)
  public Result setBackupFlag(UUID customerUUID, UUID universeUUID, Boolean markActive) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universeActionsHandler.setBackupFlag(universe, markActive);
    auditService().createAuditEntry(ctx(), request());
    return empty();
  }

  /**
   * Mark whether the universe has been made helm compatible.
   *
   * @return Result
   */
  @ApiOperation(
      value = "Set the universe as helm3 compatible",
      response = YWResults.YWSuccess.class)
  public Result setHelm3Compatible(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    universeActionsHandler.setHelm3Compatible(universe);
    auditService().createAuditEntry(ctx(), request());
    return empty();
  }

  /**
   * API that sets universe version number to -1
   *
   * @return result of settings universe version to -1 (either success if universe exists else
   *     failure
   */
  @ApiOperation(value = "resetVersion", response = YWResults.YWSuccess.class)
  public Result resetVersion(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universe.resetVersion();
    return empty();
  }
}
