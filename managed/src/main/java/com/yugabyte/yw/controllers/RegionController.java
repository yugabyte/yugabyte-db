// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.forms.YWError;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Api(value = "Region", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class RegionController extends AuthenticatedController {
  @Inject FormFactory formFactory;

  @Inject ConfigHelper configHelper;

  @Inject NetworkManager networkManager;

  @Inject CloudQueryHelper cloudQueryHelper;

  public static final Logger LOG = LoggerFactory.getLogger(RegionController.class);
  // This constant defines the minimum # of PlacementAZ we need to tag a region as Multi-PlacementAZ
  // complaint

  @ApiOperation(
      value = "list Regions for a specific provider",
      response = Region.class,
      responseContainer = "List")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = 500,
          message = "If there was a server or database issue when listing the regions",
          response = YWError.class))
  public Result list(UUID customerUUID, UUID providerUUID) {
    List<Region> regionList = null;

    try {
      int minAZCountNeeded = 1;
      regionList = Region.fetchValidRegions(customerUUID, providerUUID, minAZCountNeeded);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to list regions");
    }
    return ApiResponse.success(regionList);
  }

  @ApiOperation(
      value = "list all Regions across all providers",
      response = Region.class,
      responseContainer = "List")
  // todo: include provider field in response
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
   *
   * @return JSON response of newly created region
   */
  @ApiOperation(value = "create new region", response = Region.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "region",
          value = "region form data for new region to be created",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RegionFormData",
          required = true))
  public Result create(UUID customerUUID, UUID providerUUID) {
    Form<RegionFormData> formData = formFactory.form(RegionFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    RegionFormData form = formData.get();
    String regionCode = form.code;

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

        if (provider.code.equals("gcp")) {
          JsonNode zoneInfo =
              cloudQueryHelper.getZones(
                  region.uuid, provider.getConfig().get("CUSTOM_GCE_NETWORK"));
          if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
            region.delete();
            String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
            return ApiResponse.error(INTERNAL_SERVER_ERROR, errMsg);
          }
          // TODO(bogdan): change this and add test...
          List<String> zones = Json.fromJson(zoneInfo.get(regionCode).get("zones"), List.class);
          List<String> subnetworks =
              Json.fromJson(zoneInfo.get(regionCode).get("subnetworks"), List.class);
          if (subnetworks.size() != 1) {
            region.delete();
            throw new RuntimeException(
                "Region Bootstrap failed. Invalid number of subnets for region " + regionCode);
          }
          String subnet = subnetworks.get(0);
          region.zones = new HashSet<>();
          zones.forEach(
              zone -> {
                region.zones.add(AvailabilityZone.createOrThrow(region, zone, zone, subnet));
              });
        } else {
          // TODO: Move this to commissioner framework, Bootstrap the region with VPC, subnet etc.
          // TODO(bogdan): is this even used???
          /*
          JsonNode vpcInfo = networkManager.bootstrap(
              region.uuid, null, form.hostVpcId, form.destVpcId, form.hostVpcRegion);
          */
          JsonNode vpcInfo = networkManager.bootstrap(region.uuid, null, null /* customPayload */);
          if (vpcInfo.has("error") || !vpcInfo.has(regionCode)) {
            region.delete();
            return ApiResponse.error(INTERNAL_SERVER_ERROR, "Region Bootstrap failed.");
          }
          Map<String, String> zoneSubnets =
              Json.fromJson(vpcInfo.get(regionCode).get("zones"), Map.class);
          region.zones = new HashSet<>();
          zoneSubnets.forEach(
              (zone, subnet) -> {
                region.zones.add(AvailabilityZone.createOrThrow(region, zone, zone, subnet));
              });
        }
      } else {
        region =
            Region.create(
                provider, regionCode, form.name, form.ybImage, form.latitude, form.longitude);
      }
      auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
      return ApiResponse.success(region);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to create region: " + regionCode);
    }
  }

  /**
   * DELETE endpoint for deleting a existing Region.
   *
   * @param customerUUID Customer UUID
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether or not delete region was sucessful or not.
   */
  @ApiOperation(value = "delete", response = Object.class)
  public Result delete(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.get(customerUUID, providerUUID, regionUUID);

    if (region == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider/Region UUID:" + regionUUID);
    }

    try {
      region.disableRegionAndZones();
    } catch (Exception e) {
      return ApiResponse.error(
          INTERNAL_SERVER_ERROR, "Unable to delete Region UUID: " + regionUUID);
    }

    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    auditService().createAuditEntry(ctx(), request());
    return ApiResponse.success(responseJson);
  }
}
