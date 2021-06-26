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
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.*;
import play.mvc.Result;

import java.io.IOException;
import java.util.UUID;

@Api(value = "Provider1", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CloudProviderApiController extends AuthenticatedController {

  @Inject private CloudProviderHandler cloudProviderHandler;

  @ApiOperation(value = "listProvider", response = Provider.class, responseContainer = "List")
  public Result list(UUID customerUUID) {
    return YWResults.withData(Provider.getAll(customerUUID));
  }

  // This endpoint we are using only for deleting provider for integration test purpose. our
  // UI should call cleanup endpoint.
  @ApiOperation(value = "TEST_ONLY", hidden = true, response = YWResults.YWSuccess.class)
  public Result delete(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    cloudProviderHandler.delete(customer, provider);
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.withMessage("Deleted provider: " + providerUUID);
  }

  @ApiOperation(value = "createProvider", response = YWResults.YWTask.class)
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
