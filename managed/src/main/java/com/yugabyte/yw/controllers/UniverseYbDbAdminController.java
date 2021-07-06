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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.controllers.handlers.UniverseYbDbAdminHandler;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

import static com.yugabyte.yw.forms.YWResults.YWSuccess.withMessage;

@Api(
    value = "Universe YB Database",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseYbDbAdminController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseYbDbAdminController.class);

  @Inject private UniverseYbDbAdminHandler universeYbDbAdminHandler;

  @ApiOperation(value = "setDatabaseCredentials", response = YWResults.YWSuccess.class)
  public Result setDatabaseCredentials(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universeYbDbAdminHandler.setDatabaseCredentials(
        customer,
        universe,
        formFactory.getFormDataOrBadRequest(DatabaseSecurityFormData.class).get());

    // TODO: Missing Audit
    return withMessage("Updated security in DB.");
  }

  @ApiOperation(value = "createUserInDB", response = YWResults.YWSuccess.class)
  public Result createUserInDB(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    DatabaseUserFormData data =
        formFactory.getFormDataOrBadRequest(DatabaseUserFormData.class).get();

    universeYbDbAdminHandler.createUserInDB(customer, universe, data);

    // TODO: Missing Audit
    return withMessage("Created user in DB.");
  }

  @VisibleForTesting static final String DEPRECATED = "Deprecated.";

  @ApiOperation(
      value = "run command in shell",
      notes = "This operation is no longer supported due to security reasons",
      response = YWResults.YWError.class)
  public Result runInShell(UUID customerUUID, UUID universeUUID) {
    throw new YWServiceException(BAD_REQUEST, DEPRECATED);
  }

  @ApiOperation(
      value = "Run YSQL query against this universe",
      notes = "Only valid when platform is running in mode is `OSS`",
      response = Object.class)
  public Result runQuery(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    Form<RunQueryFormData> formData = formFactory.getFormDataOrBadRequest(RunQueryFormData.class);

    JsonNode queryResult =
        universeYbDbAdminHandler.validateRequestAndExecuteQuery(universe, formData.get());
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return YWResults.withRawData(queryResult);
  }
}
