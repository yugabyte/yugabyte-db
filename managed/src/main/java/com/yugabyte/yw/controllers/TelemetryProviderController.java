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
import com.yugabyte.yw.models.helpers.CommonUtils;
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
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Telemetry Provider",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class TelemetryProviderController extends AuthenticatedController {

  @Inject private TelemetryProviderService telemetryProviderService;

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Get Telemetry Provider",
      response = TelemetryProvider.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getTelemetryProvider(UUID customerUUID, UUID providerUUID) {
    telemetryProviderService.throwExceptionIfRuntimeFlagDisabled();
    Customer.getOrBadRequest(customerUUID);
    TelemetryProvider provider =
        telemetryProviderService.getOrBadRequest(customerUUID, providerUUID);
    return PlatformResults.withData(CommonUtils.maskObject(provider));
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "List All Telemetry Providers",
      response = TelemetryProvider.class,
      responseContainer = "List",
      nickname = "listAllTelemetryProviders")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listTelemetryProviders(UUID customerUUID) {
    telemetryProviderService.throwExceptionIfRuntimeFlagDisabled();
    Customer.getOrBadRequest(customerUUID);
    List<TelemetryProvider> providers =
        telemetryProviderService.list(customerUUID).stream()
            .map(tp -> CommonUtils.maskObject(tp))
            .collect(Collectors.toList());
    return PlatformResults.withData(providers);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Create Telemetry Provider",
      response = TelemetryProvider.class,
      nickname = "createTelemetry")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "providerData",
          dataType = "com.yugabyte.yw.models.TelemetryProvider",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createTelemetryProvider(UUID customerUUID, Http.Request request) {
    telemetryProviderService.throwExceptionIfRuntimeFlagDisabled();
    Customer.getOrBadRequest(customerUUID);
    TelemetryProvider provider = parseJson(request, TelemetryProvider.class);
    if (provider.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create provider with uuid set");
    }
    // Validate the telemetry provider config.
    log.info("Validating telemetry provider config for provider: '{}'.", provider.getName());
    telemetryProviderService.validateTelemetryProvider(provider);

    // Save TP config to DB after validation.
    provider.setCustomerUUID(customerUUID);
    provider = telemetryProviderService.save(provider);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.TelemetryProvider,
            customerUUID.toString(),
            Audit.ActionType.CreateTelemetryConfig);
    return PlatformResults.withData(provider);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Delete a telemetry provider",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteTelemetryProvider(
      UUID customerUUID, UUID providerUUID, Http.Request request) {
    telemetryProviderService.throwExceptionIfRuntimeFlagDisabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Check if telemetry provider exists.
    boolean doesTelemetryProviderExist =
        telemetryProviderService.checkIfExists(customer.getUuid(), providerUUID);
    if (!doesTelemetryProviderExist) {
      String errorMessage =
          String.format("Telemetry Provider '%s' does not exist.", providerUUID.toString());
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }

    // Check if telemetry provider is in use.
    boolean isProviderInUse = telemetryProviderService.isProviderInUse(customer, providerUUID);
    if (isProviderInUse) {
      String errorMessage =
          String.format(
              "Cannot delete Telemetry Provider '%s', as it is in use.", providerUUID.toString());
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }

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
