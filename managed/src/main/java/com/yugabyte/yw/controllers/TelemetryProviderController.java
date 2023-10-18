// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
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
import java.util.List;
import java.util.UUID;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Telemetry Provider",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class TelemetryProviderController extends AuthenticatedController {

  @Inject private TelemetryProviderService telemetryProviderService;

  @ApiOperation(
      value = "YbaApi Internal. Get Telemetry Provider",
      response = TelemetryProvider.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result getTelemetryProvider(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    TelemetryProvider provider =
        telemetryProviderService.getOrBadRequest(customerUUID, providerUUID);
    return PlatformResults.withData(provider);
  }

  @ApiOperation(
      value = "YbaApi Internal. List All Telemetry Providers",
      response = TelemetryProvider.class,
      responseContainer = "List",
      nickname = "listAllTelemetryProviders")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result listTelemetryProviders(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<TelemetryProvider> providers = telemetryProviderService.list(customerUUID);
    return PlatformResults.withData(providers);
  }

  @ApiOperation(
      value = "YbaApi Internal. Create Telemetry Provider",
      response = TelemetryProvider.class,
      nickname = "createTelemetry")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "providerData",
          dataType = "com.yugabyte.yw.models.TelemetryProvider",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result createTelemetryProvider(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    TelemetryProvider provider = parseJson(request, TelemetryProvider.class);
    if (provider.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create provider with uuid set");
    }
    provider = telemetryProviderService.save(provider);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.TelemetryProvider,
            customerUUID.toString(),
            Audit.ActionType.CreateTelemetryConfig);
    return PlatformResults.withData(provider);
  }

  @ApiOperation(value = "YbaApi Internal. Delete a telemetry provider", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result deleteTelemetryProvider(
      UUID customerUUID, UUID providerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    telemetryProviderService.delete(providerUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.TelemetryProvider,
            providerUUID.toString(),
            Audit.ActionType.DeleteTelemetryConfig);
    return PlatformResults.YBPSuccess.empty();
  }
}
