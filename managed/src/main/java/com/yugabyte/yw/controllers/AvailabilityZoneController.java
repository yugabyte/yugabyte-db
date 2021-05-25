// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.YWServiceException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.forms.AvailabilityZoneFormData;
import com.yugabyte.yw.forms.AvailabilityZoneFormData.AvailabilityZoneData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;

import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

public class AvailabilityZoneController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZoneController.class);

  @Inject ValidatingFormFactory formFactory;

  /**
   * GET endpoint for listing availability zones
   *
   * @return JSON response with availability zone's
   */
  public Result list(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);

    List<AvailabilityZone> zoneList =
        AvailabilityZone.find.query().where().eq("region", region).findList();
    return ApiResponse.success(zoneList);
  }

  /**
   * POST endpoint for creating new region(s)
   *
   * @return JSON response of newly created region(s)
   */
  public Result create(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    Form<AvailabilityZoneFormData> formData =
        formFactory.getFormDataOrBadRequest(AvailabilityZoneFormData.class);

    List<AvailabilityZoneData> azDataList = formData.get().availabilityZones;
    Map<String, AvailabilityZone> availabilityZones = new HashMap<>();
    try {
      for (AvailabilityZoneData azData : azDataList) {
        AvailabilityZone az =
            AvailabilityZone.create(region, azData.code, azData.name, azData.subnet);
        availabilityZones.put(az.code, az);
      }
      auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
      return ApiResponse.success(availabilityZones);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      AvailabilityZoneData failedAz = azDataList.get(availabilityZones.size());
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to create zone: " + failedAz.code);
    }
  }

  /**
   * DELETE endpoint for deleting a existing availability zone.
   *
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @param azUUID AvailabilityZone UUID
   * @return JSON response on whether or not delete region was successful or not.
   */
  public Result delete(UUID customerUUID, UUID providerUUID, UUID regionUUID, UUID azUUID) {
    Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    AvailabilityZone az = AvailabilityZone.getByRegionOrBadRequest(azUUID, regionUUID);

    try {
      az.setActiveFlag(false);
      az.update();
      ObjectNode responseJson = Json.newObject();
      auditService().createAuditEntry(ctx(), request());
      responseJson.put("success", true);
      return ApiResponse.success(responseJson);
    } catch (Exception e) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to flag AZ UUID as deleted: " + azUUID);
    }
  }
}
