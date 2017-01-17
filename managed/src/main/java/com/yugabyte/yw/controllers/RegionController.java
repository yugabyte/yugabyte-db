// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import com.avaje.ebean.Ebean;
import com.avaje.ebean.SqlUpdate;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import static com.avaje.ebean.Ebean.beginTransaction;
import static com.avaje.ebean.Ebean.commitTransaction;
import static com.avaje.ebean.Ebean.endTransaction;

public class RegionController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;
  public static final Logger LOG = LoggerFactory.getLogger(RegionController.class);
  // This constant defines the minimum # of PlacementAZ we need to tag a region as Multi-PlacementAZ complaint
  protected static final int MULTI_AZ_MIN_ZONE_COUNT = 3;

  /**
   * GET endpoint for listing regions
   * @return JSON response with region's
   */
  public Result list(UUID providerUUID) {
    List<Region> regionList = null;
    ObjectNode responseJson = Json.newObject();

    boolean multiAZ = false;
    if (request().getQueryString("isMultiAZ") != null) {
      multiAZ = request().getQueryString("isMultiAZ").equals("true");
    }

    try {
      int azCountNeeded = multiAZ ? MULTI_AZ_MIN_ZONE_COUNT : 1;
      regionList = Region.fetchValidRegions(providerUUID, azCountNeeded);
    } catch (Exception e) {
      responseJson.put("error", e.getMessage());
      return internalServerError(responseJson);
    }
    return ok(Json.toJson(regionList));
  }

  /**
   * GET endpoint for listing all regions across all providers
   * @return JSON response with RegionList joined with provider Name, uuid, code
   */
  public Result listAllRegions() {
    List<Provider> providerList = Provider.find.all();
    ArrayNode resultArray = Json.newArray();
    for (Provider provider : providerList) {
      List<Region> regionList = Region.fetchValidRegions(provider.uuid, 1);
      for (Region region : regionList) {
        ObjectNode regionNode = (ObjectNode) Json.toJson(region);
        regionNode.set("provider", Json.toJson(provider));
        resultArray.add(regionNode);
      }
    }
    return ok(resultArray);
  }

  /**
   * POST endpoint for creating new region
   * @return JSON response of newly created region
   */
  public Result create(UUID providerUUID) {
    Form<RegionFormData> formData = formFactory.form(RegionFormData.class).bindFromRequest();
    Provider provider = Provider.find.byId(providerUUID);
    ObjectNode responseJson = Json.newObject();

    if (provider == null) {
      responseJson.put("error", "Invalid Provider UUID");
      return badRequest(responseJson);
    }

    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }

    try {
      Region p = Region.create(provider,
                               formData.get().code,
                               formData.get().name,
                               formData.get().ybImage);
      return ok(Json.toJson(p));
    } catch (Exception e) {
      // TODO: Handle exception and print user friendly message
      responseJson.put("error", e.getMessage());
      return internalServerError(responseJson);
    }
  }

  /**
   * DELETE endpoint for deleting a existing Region.
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether or not delete region was sucessful or not.
     */
  public Result delete(UUID providerUUID, UUID regionUUID) {
    Region region = Region.find.where()
            .idEq(regionUUID).eq("provider_uuid", providerUUID).findUnique();

    if (region == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider/Region UUID:" + regionUUID);
    }

    try {
      region.disableRegionAndZones();
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to flag Region UUID as deleted: " + regionUUID);
    }

    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    return ApiResponse.success(responseJson);
  }
}
