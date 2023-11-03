// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.AvailabilityZoneHandler;
import com.yugabyte.yw.forms.AvailabilityZoneData;
import com.yugabyte.yw.forms.AvailabilityZoneEditData;
import com.yugabyte.yw.forms.AvailabilityZoneFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Availability Zones",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AvailabilityZoneController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZoneController.class);

  @Inject private AvailabilityZoneHandler availabilityZoneHandler;
  @Inject private RuntimeConfGetter runtimeConfGetter;

  /**
   * GET endpoint for listing availability zones
   *
   * @return JSON response with availability zones
   */
  @ApiOperation(
      value = "List availability zones",
      response = AvailabilityZone.class,
      responseContainer = "List",
      nickname = "listOfAZ")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);

    List<AvailabilityZone> zoneList =
        AvailabilityZone.find.query().where().eq("region", region).findList();
    return PlatformResults.withData(zoneList);
  }

  /**
   * POST endpoint for creating new zone(s)
   *
   * @return JSON response of newly created zone(s)
   */
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.18.2.0")
  @ApiOperation(
      value =
          "Deprecated since YBA version 2.18.2.0, Use "
              + "/api/v1/customers/{cUUID}/provider/{pUUID}/provider_regions/:rUUID/region_zones"
              + " instead",
      response = AvailabilityZone.class,
      responseContainer = "Map",
      nickname = "createAZ")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "azFormData",
          value = "Availability zone form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AvailabilityZoneFormData",
          required = true))
  @Deprecated
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, Http.Request request) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    Form<AvailabilityZoneFormData> formData =
        formFactory.getFormDataOrBadRequest(request, AvailabilityZoneFormData.class);
    List<AvailabilityZoneData> azDataList = formData.get().availabilityZones;
    List<String> createdAvailabilityZonesUUID = new ArrayList<>();
    Map<String, AvailabilityZone> availabilityZones = new HashMap<>();
    List<AvailabilityZone> createdZones = availabilityZoneHandler.createZones(region, azDataList);
    for (AvailabilityZone az : createdZones) {
      availabilityZones.put(az.getCode(), az);
      createdAvailabilityZonesUUID.add(az.getUuid().toString());
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AvailabilityZone,
            createdAvailabilityZonesUUID.toString(),
            Audit.ActionType.Create);
    return PlatformResults.withData(availabilityZones);
  }

  /**
   * POST endpoint for creating new zone(s)
   *
   * @return JSON response of newly created zone(s)
   */
  @ApiOperation(
      value = "WARNING: This is a preview API that could change. Create an availability zone",
      response = AvailabilityZone.class,
      responseContainer = "Map",
      nickname = "createZone")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "azData",
          value = "Availability Zone to create",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AvailabilityZone",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.18.2.0")
  public Result createZoneNew(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, Http.Request request) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZone zone =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), AvailabilityZone.class);
    zone = availabilityZoneHandler.createZone(region, zone);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AvailabilityZone,
            zone.getUuid().toString(),
            Audit.ActionType.Create);
    return PlatformResults.withData(zone);
  }

  /**
   * PUT endpoint for editing an availability zone
   *
   * @return JSON response of the modified zone
   */
  @ApiOperation(
      value = "WARNING: This is a preview API that could change. Modify an availability zone",
      response = AvailabilityZone.class,
      nickname = "editZone")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "azData",
          value = "Availability zone to edit",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AvailabilityZone",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.18.2.0")
  public Result editZoneNew(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID zoneUUID, Http.Request request) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZone raz =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), AvailabilityZone.class);
    AvailabilityZone az =
        availabilityZoneHandler.editZone(
            zoneUUID,
            regionUUID,
            zone -> {
              zone.setSubnet(raz.getSubnet());
              zone.setSecondarySubnet(raz.getSecondarySubnet());
              zone.setDetails(raz.getDetails());
            });

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AvailabilityZone,
            az.getUuid().toString(),
            Audit.ActionType.Edit);
    return PlatformResults.withData(az);
  }

  /**
   * PUT endpoint for editing an availability zone
   *
   * @return JSON response of the modified zone
   */
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.18.2.0")
  @ApiOperation(
      value =
          "Deprecated since YBA version 2.18.2.0, Use "
              + "/api/v1/customers/{cUUID}/provider/{pUUID}/provider_regions/:rUUID/region_zones/:zUUID"
              + " instead",
      response = AvailabilityZone.class,
      nickname = "editAZ")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "azFormData",
          value = "Availability zone edit form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AvailabilityZoneFormData",
          required = true))
  @Deprecated
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result edit(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID zoneUUID, Http.Request request) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZoneEditData azData =
        formFactory.getFormDataOrBadRequest(request, AvailabilityZoneEditData.class).get();
    AvailabilityZone az =
        availabilityZoneHandler.editZone(
            zoneUUID,
            regionUUID,
            zone -> {
              zone.setSubnet(azData.subnet);
              zone.setSecondarySubnet(azData.secondarySubnet);
            });

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AvailabilityZone,
            az.getUuid().toString(),
            Audit.ActionType.Edit);
    return PlatformResults.withData(az);
  }

  /**
   * DELETE endpoint for deleting an existing availability zone.
   *
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @param azUUID AvailabilityZone UUID
   * @return empty response
   */
  @ApiOperation(
      value = "Delete an availability zone",
      response = YBPSuccess.class,
      nickname = "deleteAZ")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID azUUID, Http.Request request) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZone az = availabilityZoneHandler.deleteZone(azUUID, regionUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.AvailabilityZone,
            az.getUuid().toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }
}
