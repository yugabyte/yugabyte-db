// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.troubleshooting.TroubleshootingPlatformService;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.TroubleshootingPlatformExt;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.filters.TroubleshootingPlatformFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.*;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Troubleshooting Platform",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class TroubleshootingPlatformController extends AuthenticatedController {

  @Inject private TroubleshootingPlatformService troubleshootingPlatformService;

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Get Troubleshooting Platform",
      response = TroubleshootingPlatformExt.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getTroubleshootingPlatform(UUID customerUUID, UUID platformUUID) {
    Customer.getOrBadRequest(customerUUID);
    TroubleshootingPlatform platform =
        troubleshootingPlatformService.getOrBadRequest(customerUUID, platformUUID);
    return PlatformResults.withData(enrich(platform));
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "List All Troubleshooting Platforms",
      response = TroubleshootingPlatformExt.class,
      responseContainer = "List",
      nickname = "listAllTroubleshootingPlatforms")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listTroubleshootingPlatforms(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    TroubleshootingPlatformFilter filter =
        TroubleshootingPlatformFilter.builder().customerUuid(customerUUID).build();
    List<TroubleshootingPlatform> platforms = troubleshootingPlatformService.list(filter);
    List<TroubleshootingPlatformExt> platformExtList =
        platforms.stream().map(this::enrich).toList();
    return PlatformResults.withData(platformExtList);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Create Troubleshooting Platform",
      response = TroubleshootingPlatform.class,
      nickname = "createTroubleshootingPlatform")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "platformData",
          dataType = "com.yugabyte.yw.models.TroubleshootingPlatform",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createTroubleshootingPlatform(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    TroubleshootingPlatform platform = parseJson(request, TroubleshootingPlatform.class);
    if (platform.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create platform with uuid set");
    }
    platform.setCustomerUUID(customerUUID);
    platform = troubleshootingPlatformService.save(platform, false);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.TroubleshootingPlatform,
            customerUUID.toString(),
            Audit.ActionType.CreateTroubleshootingConfig);
    return PlatformResults.withData(platform);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Edit Troubleshooting Platform",
      response = TroubleshootingPlatform.class,
      nickname = "editTroubleshootingPlatform")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "platformData",
          dataType = "com.yugabyte.yw.models.TroubleshootingPlatform",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editTroubleshootingPlatform(
      UUID customerUUID, UUID tpUUID, boolean force, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    TroubleshootingPlatform platform = parseJson(request, TroubleshootingPlatform.class);
    if (platform.getUuid() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't edit platform without uuid");
    }
    if (!platform.getUuid().equals(tpUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Platform uuids do not match");
    }
    platform.setCustomerUUID(customerUUID);
    TroubleshootingPlatform currentPlatform =
        troubleshootingPlatformService.getOrBadRequest(customerUUID, tpUUID);
    platform = CommonUtils.unmaskObject(currentPlatform, platform);
    platform = troubleshootingPlatformService.save(platform, force);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.TroubleshootingPlatform,
            customerUUID.toString(),
            Audit.ActionType.EditTroubleshootingConfig);
    return PlatformResults.withData(platform);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Delete Troubleshooting Platform",
      response = Boolean.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteTroubleshootingPlatform(
      UUID customerUUID, UUID platformUUID, boolean force, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    troubleshootingPlatformService.delete(customerUUID, platformUUID, force);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.TroubleshootingPlatform,
            platformUUID.toString(),
            Audit.ActionType.DeleteTroubleshootingConfig);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Check if universe is registered with Troubleshooting Platform",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result checkRegistered(
      UUID customerUUID, UUID platformUUID, UUID universeUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    TroubleshootingPlatform platform =
        troubleshootingPlatformService.getOrBadRequest(customerUUID, platformUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    boolean isRegistered = troubleshootingPlatformService.isRegistered(platform, universe);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.TroubleshootingPlatformRegister);
    if (isRegistered) {
      return YBPSuccess.empty();
    } else {
      throw new PlatformServiceException(
          NOT_FOUND, "Universe is not registered with Troubleshooting Platform");
    }
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Register universe with Troubleshooting Platform",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result registerUniverse(
      UUID customerUUID, UUID platformUUID, UUID universeUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    TroubleshootingPlatform platform =
        troubleshootingPlatformService.getOrBadRequest(customerUUID, platformUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    troubleshootingPlatformService.putUniverse(platform, universe);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.TroubleshootingPlatformRegister);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Unregister universe from Troubleshooting Platform",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result unregisterUniverse(
      UUID customerUUID, UUID platformUUID, UUID universeUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    TroubleshootingPlatform platform =
        troubleshootingPlatformService.getOrBadRequest(customerUUID, platformUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    troubleshootingPlatformService.deleteUniverse(platform, universe);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.TroubleshootingPlatformUnregister);
    return YBPSuccess.empty();
  }

  private TroubleshootingPlatformExt enrich(TroubleshootingPlatform platform) {
    TroubleshootingPlatformExt platformExt = new TroubleshootingPlatformExt();
    platformExt.setTroubleshootingPlatform(CommonUtils.maskObject(platform));
    platformExt.setInUseStatus(troubleshootingPlatformService.getInUseStatus(platform));
    return platformExt;
  }
}
