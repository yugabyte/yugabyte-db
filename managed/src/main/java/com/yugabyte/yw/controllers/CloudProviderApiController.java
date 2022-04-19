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
import com.google.api.client.util.Throwables;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.util.Objects;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Cloud providers",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class CloudProviderApiController extends AuthenticatedController {

  @Inject private CloudProviderHandler cloudProviderHandler;

  @ApiOperation(
      value = "List cloud providers",
      response = Provider.class,
      responseContainer = "List",
      nickname = "getListOfProviders")
  public Result list(UUID customerUUID, String name, String code) {
    CloudType providerCode = code == null ? null : CloudType.valueOf(code);
    return PlatformResults.withData(Provider.getAll(customerUUID, name, providerCode));
  }

  @ApiOperation(
      value = "Delete a cloud provider",
      notes = "This endpoint is used only for integration tests.",
      hidden = true,
      response = YBPSuccess.class)
  public Result delete(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    cloudProviderHandler.delete(customer, provider);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.withMessage("Deleted provider: " + providerUUID);
  }

  @ApiOperation(
      value = "Refresh pricing",
      notes = "Refresh provider pricing info",
      response = YBPSuccess.class)
  public Result refreshPricing(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    cloudProviderHandler.refreshPricing(customerUUID, provider);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.RefreshPricing);
    return YBPSuccess.withMessage(provider.code.toUpperCase() + " Initialized");
  }

  @ApiOperation(value = "Update a provider", response = YBPTask.class, nickname = "editProvider")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "edit provider form data",
          name = "EditProviderRequest",
          dataType = "com.yugabyte.yw.models.Provider",
          required = true,
          paramType = "body"))
  public Result edit(UUID customerUUID, UUID providerUUID) throws IOException {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Provider editProviderReq =
        formFactory.getFormDataOrBadRequest(request().body().asJson(), Provider.class);
    UUID taskUUID = cloudProviderHandler.editProvider(customer, provider, editProviderReq);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.Update,
            Json.toJson(editProviderReq));
    return new YBPTask(taskUUID, providerUUID).asResult();
  }

  @ApiOperation(value = "Create a provider", response = YBPTask.class, nickname = "createProviders")
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

    CloudType providerCode = CloudType.valueOf(reqProvider.code);
    Provider providerEbean;
    if (providerCode.equals(CloudType.kubernetes)) {
      providerEbean = cloudProviderHandler.createKubernetesNew(customer, reqProvider);
    } else {
      providerEbean =
          cloudProviderHandler.createProvider(
              customer,
              providerCode,
              reqProvider.name,
              reqProvider.getUnmaskedConfig(),
              getFirstRegionCode(reqProvider));
    }

    if (providerCode.isRequiresBootstrap()) {
      UUID taskUUID = null;
      try {
        CloudBootstrap.Params taskParams = CloudBootstrap.Params.fromProvider(reqProvider);

        taskUUID = cloudProviderHandler.bootstrap(customer, providerEbean, taskParams);
        auditService()
            .createAuditEntryWithReqBody(
                ctx(),
                Audit.TargetType.CloudProvider,
                Objects.toString(providerEbean.uuid, null),
                Audit.ActionType.Create,
                requestBody,
                taskUUID);
      } catch (Throwable e) {
        log.warn("Bootstrap failed. Deleting provider");
        providerEbean.delete();
        Throwables.propagate(e);
      }
      return new YBPTask(taskUUID, providerEbean.uuid).asResult();
    } else {
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.CloudProvider,
              Objects.toString(providerEbean.uuid, null),
              Audit.ActionType.Create,
              requestBody,
              null);
      return new YBPTask(null, providerEbean.uuid).asResult();
    }
  }

  private static String getFirstRegionCode(Provider provider) {
    for (Region r : provider.regions) {
      return r.code;
    }
    return null;
  }
}
