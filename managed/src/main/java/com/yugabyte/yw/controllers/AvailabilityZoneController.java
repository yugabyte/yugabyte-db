// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.forms.AvailabilityZoneFormData;
import com.yugabyte.yw.forms.AvailabilityZoneFormData.AvailabilityZoneData;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Api(
    value = "AvailabilityZone",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AvailabilityZoneController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZoneController.class);

  /**
   * GET endpoint for listing availability zones
   *
   * @return JSON response with availability zone's
   */
  @ApiOperation(value = "listAZ", response = AvailabilityZone.class, responseContainer = "List")
  public Result list(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);

    List<AvailabilityZone> zoneList =
        AvailabilityZone.find.query().where().eq("region", region).findList();
    return YWResults.withData(zoneList);
  }

  /**
   * POST endpoint for creating new region(s)
   *
   * @return JSON response of newly created region(s)
   */
  @ApiOperation(value = "createAZ", response = AvailabilityZone.class, responseContainer = "Map")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "azFormData",
          value = "az form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AvailabilityZoneFormData",
          required = true))
  public Result create(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    Form<AvailabilityZoneFormData> formData =
        formFactory.getFormDataOrBadRequest(AvailabilityZoneFormData.class);

    List<AvailabilityZoneData> azDataList = formData.get().availabilityZones;
    Map<String, AvailabilityZone> availabilityZones = new HashMap<>();
    for (AvailabilityZoneData azData : azDataList) {
      AvailabilityZone az =
          AvailabilityZone.createOrThrow(region, azData.code, azData.name, azData.subnet);
      availabilityZones.put(az.code, az);
    }
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return YWResults.withData(availabilityZones);
  }

  /**
   * DELETE endpoint for deleting a existing availability zone.
   *
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @param azUUID AvailabilityZone UUID
   * @return JSON response on whether or not delete region was successful or not.
   */
  @ApiOperation(value = "deleteAZ", response = Object.class)
  public Result delete(UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID azUUID) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZone az = AvailabilityZone.getByRegionOrBadRequest(azUUID, regionUUID);
    az.setActiveFlag(false);
    az.update();
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }
}
