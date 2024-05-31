// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.RegionHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.RegionEditFormData;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Region management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class RegionController extends AuthenticatedController {

  @Inject RegionHandler regionHandler;

  public static final Logger LOG = LoggerFactory.getLogger(RegionController.class);

  // This constant defines the minimum # of PlacementAZ we need to tag a region as Multi-PlacementAZ
  // complaint

  @ApiOperation(
      value = "List a provider's regions",
      response = Region.class,
      responseContainer = "List",
      nickname = "getRegion")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = 500,
          message = "If there was a server or database issue when listing the regions",
          response = YBPError.class))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, UUID providerUUID) {
    List<Region> regionList;

    int minAZCountNeeded = 1;
    regionList = Region.fetchValidRegions(customerUUID, providerUUID, minAZCountNeeded);
    return PlatformResults.withData(regionList);
  }

  @ApiOperation(
      value = "List regions for all providers",
      response = Region.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  // todo: include provider field in response
  public Result listAllRegions(UUID customerUUID) {
    Set<UUID> providerUuids =
        Provider.getAll(customerUUID).stream().map(Provider::getUuid).collect(Collectors.toSet());
    List<Region> regionList = Region.getFullByProviders(providerUuids);
    List<JsonNode> result =
        regionList.stream()
            .peek(region -> CloudInfoInterface.mayBeMassageResponse(region.getProvider(), region))
            .map(
                region -> {
                  ObjectNode regionNode = (ObjectNode) Json.toJson(region);
                  regionNode.set("provider", Json.toJson(region.getProvider()));
                  return regionNode;
                })
            .collect(Collectors.toList());
    return PlatformResults.withData(result);
  }

  /**
   * POST endpoint for creating new region
   *
   * @return JSON response of newly created region
   */
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.18.2.0")
  @ApiOperation(
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.18.2.0.</b></p>"
              + "Use /api/v1/customers/{cUUID}/provider/{pUUID}/provider_regions instead",
      value = "Create Region - deprecated",
      response = Region.class,
      nickname = "createRegion")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "region",
          value = "region form data for new region to be created",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RegionFormData",
          required = true))
  @Deprecated
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, UUID providerUUID, Http.Request request) {
    Form<RegionFormData> formData =
        formFactory.getFormDataOrBadRequest(request, RegionFormData.class);
    RegionFormData form = formData.get();
    Region region = regionHandler.createRegion(customerUUID, providerUUID, form);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Region,
            Objects.toString(region.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(region);
  }

  /**
   * POST endpoint for creating new region
   *
   * @return JSON response of newly created region
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Create a new region",
      response = Region.class,
      nickname = "createProviderRegion")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "region",
          value = "Specification of Region to be created",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.Region",
          required = true))
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.18.2.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createRegionNew(UUID customerUUID, UUID providerUUID, Http.Request request) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Region region = formFactory.getFormDataOrBadRequest(request.body().asJson(), Region.class);
    region.setProviderCode(provider.getCode());
    region = regionHandler.createRegion(customerUUID, providerUUID, region);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Region,
            Objects.toString(region.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(region);
  }

  /**
   * PUT endpoint for modifying an existing Region.
   *
   * @param customerUUID Customer UUID
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether or not the operation was successful.
   */
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.18.2.0")
  @ApiOperation(
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.18.2.0.</b></p>"
              + "Use /api/v1/customers/{cUUID}/provider/{pUUID}/provider_regions instead",
      value = "Edit regions - deprecated",
      response = Object.class,
      nickname = "editRegion")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "region",
          value = "region edit form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RegionFormData",
          required = true))
  @Deprecated
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result edit(UUID customerUUID, UUID providerUUID, UUID regionUUID, Http.Request request) {
    RegionEditFormData form =
        formFactory.getFormDataOrBadRequest(request, RegionEditFormData.class).get();
    Region region = regionHandler.editRegion(customerUUID, providerUUID, regionUUID, form);

    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Region, regionUUID.toString(), Audit.ActionType.Edit);
    return PlatformResults.withData(region);
  }

  /**
   * PUT endpoint for modifying an existing Region.
   *
   * @param customerUUID Customer UUID
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether or not the operation was successful.
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Modify a region",
      response = Region.class,
      nickname = "editProviderRegion")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "region",
          value = "Specification of Region to be edited",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.Region",
          required = true))
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.18.2.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editRegionNew(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, Http.Request request) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);

    Region region = formFactory.getFormDataOrBadRequest(request.body().asJson(), Region.class);
    region.setProviderCode(provider.getCode());
    region = regionHandler.editRegion(customerUUID, providerUUID, regionUUID, region);

    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Region, regionUUID.toString(), Audit.ActionType.Edit);
    return PlatformResults.withData(region);
  }

  /**
   * DELETE endpoint for deleting an existing Region.
   *
   * @param customerUUID Customer UUID
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether the region was successfully deleted.
   */
  @ApiOperation(value = "Delete a region", response = Object.class, nickname = "deleteRegion")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, Http.Request request) {
    Provider.getOrBadRequest(customerUUID, providerUUID);
    Region region = regionHandler.deleteRegion(customerUUID, providerUUID, regionUUID);

    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Region, regionUUID.toString(), Audit.ActionType.Delete);
    return PlatformResults.withData(region);
  }
}
