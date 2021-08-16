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
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.forms.EditProviderRequest;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.forms.YWResults.YWSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.util.UUID;
import play.libs.Json;
import play.mvc.Result;

@Api(value = "Provider", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CloudProviderApiController extends AuthenticatedController {

  @Inject private CloudProviderHandler cloudProviderHandler;

  @ApiOperation(
      value = "listProvider",
      response = Provider.class,
      responseContainer = "List",
      nickname = "getListOfProviders")
  public Result list(UUID customerUUID) {
    return YWResults.withData(Provider.getAll(customerUUID));
  }

  @ApiOperation(
      value = "deleteProvider",
      notes = "This endpoint we are using only for deleting provider for integration test purpose.",
      response = YWResults.YWSuccess.class)
  public Result delete(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    cloudProviderHandler.delete(customer, provider);
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.withMessage("Deleted provider: " + providerUUID);
  }

  @ApiOperation(value = "refreshPricing", notes = "Refresh Provider pricing info")
  public Result refreshPricing(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    cloudProviderHandler.refreshPricing(customerUUID, provider);
    auditService().createAuditEntry(ctx(), request());
    return YWSuccess.withMessage(provider.code.toUpperCase() + " Initialized");
  }

  @ApiOperation(value = "editProvider", response = Provider.class, nickname = "editProvider")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "edit provider form data",
          name = "EditProviderFormData",
          dataType = "com.yugabyte.yw.forms.EditProviderRequest",
          required = true,
          paramType = "body"))
  public Result edit(UUID customerUUID, UUID providerUUID) throws IOException {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    EditProviderRequest editProviderReq =
        formFactory.getFormDataOrBadRequest(request().body().asJson(), EditProviderRequest.class);
    cloudProviderHandler.editProvider(provider, editProviderReq);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(editProviderReq));
    return YWResults.withData(provider);
  }

  @ApiOperation(
      value = "createProvider",
      response = YWResults.YWTask.class,
      nickname = "createProviders")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateProviderRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.Provider",
          required = true))
  public Result create(UUID customerUUID) throws IOException {
    JsonNode requestBody = request().body().asJson();
    Provider reqProvider = formFactory.getFormDataOrBadRequest(requestBody, Provider.class);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    reqProvider.customerUUID = customerUUID;
    Provider providerEbean =
        cloudProviderHandler.createProvider(
            customer,
            Common.CloudType.valueOf(reqProvider.code),
            reqProvider.name,
            reqProvider.getConfig(),
            getFirstRegionCode(reqProvider));

    CloudBootstrap.Params taskParams = CloudBootstrap.Params.fromProvider(reqProvider);

    UUID taskUUID = cloudProviderHandler.bootstrap(customer, providerEbean, taskParams);
    auditService().createAuditEntry(ctx(), request(), requestBody, taskUUID);
    return new YWResults.YWTask(taskUUID, providerEbean.uuid).asResult();
  }

  private static String getFirstRegionCode(Provider provider) {
    for (Region r : provider.regions) {
      return r.code;
    }
    return null;
  }
}
