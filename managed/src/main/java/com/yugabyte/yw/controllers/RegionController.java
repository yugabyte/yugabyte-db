// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.models.AvailabilityZone;
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


public class RegionController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;

  @Inject
  ConfigHelper configHelper;

  @Inject
  NetworkManager networkManager;

  public static final Logger LOG = LoggerFactory.getLogger(RegionController.class);
  // This constant defines the minimum # of PlacementAZ we need to tag a region as Multi-PlacementAZ complaint
  protected static final int MULTI_AZ_MIN_ZONE_COUNT = 3;

  /**
   * GET endpoint for listing regions
   * @return JSON response with region's
   */
  public Result list(UUID customerUUID, UUID providerUUID) {
    List<Region> regionList = null;

    boolean multiAZ = false;
    if (request().getQueryString("isMultiAZ") != null) {
      multiAZ = request().getQueryString("isMultiAZ").equals("true");
    }

    try {
      int azCountNeeded = multiAZ ? MULTI_AZ_MIN_ZONE_COUNT : 1;
      regionList = Region.fetchValidRegions(customerUUID, providerUUID, azCountNeeded);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to list regions");
    }
    return ApiResponse.success(regionList);
  }

  /**
   * GET endpoint for listing all regions across all providers
   * @return JSON response with RegionList joined with provider Name, uuid, code
   */
  public Result listAllRegions(UUID customerUUID) {
    List<Provider> providerList = Provider.getAll(customerUUID);
    ArrayNode resultArray = Json.newArray();
    for (Provider provider : providerList) {
      List<Region> regionList = Region.fetchValidRegions(customerUUID, provider.uuid, 1);
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
  public Result create(UUID customerUUID, UUID providerUUID) {
    Form<RegionFormData> formData = formFactory.form(RegionFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    String regionCode = formData.get().code;

    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID:" + providerUUID);
    }

    if (Region.getByCode(provider, regionCode) != null) {
      return ApiResponse.error(BAD_REQUEST, "Region code already exists: " + regionCode);
    }

    try {
      Map<String, Object> regionMetadata =
          configHelper.getRegionMetadata(Common.CloudType.valueOf(provider.code));

      Region region;
      // If we have region metadata we create the region with that metadata or else we assume
      // some metadata is passed in (esp for onprem case).
      if (regionMetadata.containsKey(regionCode)) {
        JsonNode metaData = Json.toJson(regionMetadata.get(regionCode));
        region = Region.createWithMetadata(provider, regionCode, metaData);
        // TODO: Move this to commissioner framework, Bootstrap the region with VPC, subnet etc.
        JsonNode vpcInfo = networkManager.bootstrap(region.uuid, formData.get().hostVPCId);
        if (vpcInfo.has("error") || !vpcInfo.has(regionCode)) {
          region.delete();
          return ApiResponse.error(INTERNAL_SERVER_ERROR, "Region Bootstrap failed.");
        }
        Map<String, String> zoneSubnets =
            Json.fromJson(vpcInfo.get(regionCode).get("zones"), Map.class);
        region.zones = new HashSet<>();
        zoneSubnets.forEach((zone, subnet) -> {
          region.zones.add(AvailabilityZone.create(region, zone, zone, subnet));
        });
      } else {
        region = Region.create(provider, regionCode, formData.get().name, formData.get().ybImage);
      }
      return ApiResponse.success(region);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to create region: " + regionCode);
    }
  }

  /**
   * DELETE endpoint for deleting a existing Region.
   * @param customerUUID Customer UUID
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether or not delete region was sucessful or not.
     */
  public Result delete(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.get(customerUUID, providerUUID, regionUUID);

    if (region == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider/Region UUID:" + regionUUID);
    }

    try {
      region.disableRegionAndZones();
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete Region UUID: " + regionUUID);
    }

    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    return ApiResponse.success(responseJson);
  }
}
