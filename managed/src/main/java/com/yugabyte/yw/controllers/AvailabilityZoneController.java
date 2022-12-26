// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.AvailabilityZoneFormData;
import com.yugabyte.yw.forms.AvailabilityZoneFormData.AvailabilityZoneData;
import com.yugabyte.yw.forms.AvailabilityZoneFormData.AvailabilityZoneEditData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
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
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Availability Zones",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AvailabilityZoneController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZoneController.class);

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
  @ApiOperation(
      value = "Create an availability zone",
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
  public Result create(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    Form<AvailabilityZoneFormData> formData =
        formFactory.getFormDataOrBadRequest(AvailabilityZoneFormData.class);

    List<AvailabilityZoneData> azDataList = formData.get().availabilityZones;
    Map<String, AvailabilityZone> availabilityZones = new HashMap<>();
    List<String> createdAvailabilityZonesUUID = new ArrayList<>();
    for (AvailabilityZoneData azData : azDataList) {
      AvailabilityZone az =
          AvailabilityZone.createOrThrow(
              region, azData.code, azData.name, azData.subnet, azData.secondarySubnet);
      availabilityZones.put(az.code, az);
      createdAvailabilityZonesUUID.add(az.uuid.toString());
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AvailabilityZone,
            createdAvailabilityZonesUUID.toString(),
            Audit.ActionType.Create,
            Json.toJson(formData.rawData()));
    return PlatformResults.withData(availabilityZones);
  }

  private void failDueToAZInUse(long nodeCount, String action) {
    throw new PlatformServiceException(
        FORBIDDEN,
        String.format(
            "There %s %d node%s in this AZ, cannot %s",
            nodeCount > 1 ? "are" : "is", nodeCount, nodeCount > 1 ? "s" : "", action));
  }

  /**
   * PUT endpoint for editing an availability zone
   *
   * @return JSON response of the modified zone
   */
  @ApiOperation(
      value = "Modify an availability zone",
      response = AvailabilityZone.class,
      nickname = "editAZ")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "azFormData",
          value = "Availability zone edit form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AvailabilityZoneEditData",
          required = true))
  public Result edit(UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID zoneUUID) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZoneEditData azData =
        formFactory.getFormDataOrBadRequest(AvailabilityZoneEditData.class).get();
    AvailabilityZone az = AvailabilityZone.getByRegionOrBadRequest(zoneUUID, regionUUID);

    long nodeCount = az.getNodeCount();
    if (nodeCount > 0) {
      failDueToAZInUse(nodeCount, "modify");
    }

    az.subnet = azData.subnet;
    az.secondarySubnet = azData.secondarySubnet;

    az.update();

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AvailabilityZone,
            az.uuid.toString(),
            Audit.ActionType.Edit,
            Json.toJson(azData));
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
  public Result delete(UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID azUUID) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZone az = AvailabilityZone.getByRegionOrBadRequest(azUUID, regionUUID);

    if (Provider.getOrBadRequest(customerUUID, providerUUID).getCloudCode()
        == Common.CloudType.onprem) {
      az.setActiveFlag(false);
      az.update();
    } else {
      long nodeCount = az.getNodeCount();
      if (nodeCount > 0) {
        failDueToAZInUse(nodeCount, "delete");
      }

      az.delete();
    }

    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.AvailabilityZone, az.uuid.toString(), Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }
}
