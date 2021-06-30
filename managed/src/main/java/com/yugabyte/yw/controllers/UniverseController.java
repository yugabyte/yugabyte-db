// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

import java.util.UUID;

@Api(value = "Universe", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @Inject private UniverseCRUDHandler universeCRUDHandler;

  /** List the universes for a given customer. */
  @ApiOperation(value = "List Universes", response = UniverseResp.class, responseContainer = "List")
  public Result list(UUID customerUUID, String name) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Verify the customer is present.
    if (name != null) {
      LOG.info("Finding Universe with name {}.", name);
      return YWResults.withData(universeCRUDHandler.findByName(name));
    }
    return YWResults.withData(universeCRUDHandler.list(customer));
  }

  @ApiOperation(value = "getUniverse", response = UniverseResp.class)
  public Result index(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    return YWResults.withData(
        UniverseResp.create(universe, null, runtimeConfigFactory.globalRuntimeConf()));
  }

  @ApiOperation(value = "Destroy the universe", response = YWResults.YWTask.class)
  public Result destroy(
      UUID customerUUID, UUID universeUUID, boolean isForceDelete, boolean isDeleteBackups) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeCRUDHandler.destroy(customer, universe, isForceDelete, isDeleteBackups);
    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }
}
