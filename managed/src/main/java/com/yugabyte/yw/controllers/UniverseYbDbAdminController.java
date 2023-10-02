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

import static com.yugabyte.yw.forms.PlatformResults.YBPSuccess.withMessage;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.UniverseYbDbAdminHandler;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.ConfigureYCQLFormData;
import com.yugabyte.yw.forms.ConfigureYSQLFormData;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.DatabaseUserDropFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import play.data.Form;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Universe database management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseYbDbAdminController extends AuthenticatedController {
  @Inject private UniverseYbDbAdminHandler universeYbDbAdminHandler;

  @ApiOperation(
      value = "Set a universe's database credentials",
      nickname = "setDatabaseCredentials",
      response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "DatabaseSecurityFormData",
          value = "The database credentials",
          required = true,
          dataType = "com.yugabyte.yw.forms.DatabaseSecurityFormData",
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result setDatabaseCredentials(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    universeYbDbAdminHandler.setDatabaseCredentials(
        customer,
        universe,
        formFactory.getFormDataOrBadRequest(request, DatabaseSecurityFormData.class).get());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.SetDBCredentials);
    return withMessage("Updated user in DB.");
  }

  @ApiOperation(
      value = "Drop a database user for a universe",
      nickname = "dropUserInDB",
      response = YBPSuccess.class,
      hidden = true)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "DatabaseUserDropFormData",
          value = "The database user to drop",
          required = true,
          dataType = "com.yugabyte.yw.forms.DatabaseUserDropFormData",
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result dropUserInDB(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    DatabaseUserDropFormData data =
        formFactory.getFormDataOrBadRequest(request, DatabaseUserDropFormData.class).get();

    universeYbDbAdminHandler.dropUser(customer, universe, data);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.DropUserInDB);
    return withMessage("Deleted user in DB.");
  }

  @ApiOperation(
      value = "Create a restricted user for a universe",
      nickname = "createRestrictedUserInDB",
      response = YBPSuccess.class,
      hidden = true)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "DatabaseUserFormData",
          value = "The database user to create",
          required = true,
          dataType = "com.yugabyte.yw.forms.DatabaseUserFormData",
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result createRestrictedUserInDB(
      UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    DatabaseUserFormData data =
        formFactory.getFormDataOrBadRequest(request, DatabaseUserFormData.class).get();

    universeYbDbAdminHandler.createRestrictedUser(customer, universe, data);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreateRestrictedUserInDB);
    return withMessage("Created restricted user in DB.");
  }

  @ApiOperation(
      value = "Create a database user for a universe",
      nickname = "createUserInDB",
      response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "DatabaseUserFormData",
          value = "The database user to create",
          required = true,
          dataType = "com.yugabyte.yw.forms.DatabaseUserFormData",
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result createUserInDB(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    DatabaseUserFormData data =
        formFactory.getFormDataOrBadRequest(request, DatabaseUserFormData.class).get();

    universeYbDbAdminHandler.createUserInDB(customer, universe, data);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreateUserInDB);
    return withMessage("Created user in DB.");
  }

  @VisibleForTesting static final String DEPRECATED = "Deprecated.";

  @ApiOperation(
      value = "Run a YSQL query in a universe",
      notes = "Runs a YSQL query. Only valid when the platform is running in `OSS` mode.",
      nickname = "runYsqlQueryUniverse",
      response = Object.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "RunQueryFormData",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RunQueryFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result runQuery(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    Form<RunQueryFormData> formData =
        formFactory.getFormDataOrBadRequest(request, RunQueryFormData.class);

    JsonNode queryResult =
        universeYbDbAdminHandler.validateRequestAndExecuteQuery(universe, formData.get(), request);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RunYsqlQuery);
    return PlatformResults.withRawData(queryResult);
  }

  /**
   * API that configure YSQL API for the universe. Only supports rolling upgrade.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Configure YSQL",
      notes = "Queues a task to configure ysql in a universe.",
      nickname = "configureYSQL",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "configure_ysql_form_data",
          value = "Configure YSQL Form Data",
          dataType = "com.yugabyte.yw.forms.ConfigureYSQLFormData",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result configureYSQL(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    ConfigureDBApiParams requestParams = getGeneralizedConfigureDBApiParams(request, universe);
    ConfigureYSQLFormData formData = parseJsonAndValidate(request, ConfigureYSQLFormData.class);
    formData.mergeWithConfigureDBApiParams(requestParams);
    UUID taskUUID = universeYbDbAdminHandler.configureYSQL(requestParams, customer, universe);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.Configure);
    return new YBPTask(taskUUID, universe.getUniverseUUID()).asResult();
  }

  /**
   * API that configure YCQL API for the universe. Only supports rolling upgrade.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Configure YCQL",
      notes = "Queues a task to configure ycql in a universe.",
      nickname = "configureYCQL",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "configure_ycql_form_data",
          value = "Configure YCQL Form Data",
          dataType = "com.yugabyte.yw.forms.ConfigureYCQLFormData",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result configureYCQL(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    ConfigureDBApiParams requestParams = getGeneralizedConfigureDBApiParams(request, universe);
    ConfigureYCQLFormData formData = parseJsonAndValidate(request, ConfigureYCQLFormData.class);
    formData.mergeWithConfigureDBApiParams(requestParams);
    UUID taskUUID = universeYbDbAdminHandler.configureYCQL(requestParams, customer, universe);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.Configure);
    return new YBPTask(taskUUID, universe.getUniverseUUID()).asResult();
  }

  private ConfigureDBApiParams getGeneralizedConfigureDBApiParams(
      Http.Request request, Universe universe) {
    ConfigureDBApiParams requestParams =
        UniverseControllerRequestBinder.bindFormDataToUpgradeTaskParams(
            request, ConfigureDBApiParams.class, universe);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    requestParams.setUniverseUUID(universe.getUniverseUUID());
    requestParams.enableYCQL = userIntent.enableYCQL;
    requestParams.enableYCQLAuth = userIntent.enableYCQLAuth;
    requestParams.enableYSQL = userIntent.enableYSQL;
    requestParams.enableYSQLAuth = userIntent.enableYSQLAuth;
    requestParams.communicationPorts = universe.getUniverseDetails().communicationPorts;
    return requestParams;
  }
}
