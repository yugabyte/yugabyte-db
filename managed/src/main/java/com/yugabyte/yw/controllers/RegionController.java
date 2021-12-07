// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Region management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class RegionController extends AuthenticatedController {
  @Inject ConfigHelper configHelper;

  @Inject NetworkManager networkManager;

  @Inject CloudQueryHelper cloudQueryHelper;

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
  // todo: include provider field in response
  public Result listAllRegions(UUID customerUUID) {
    List<Provider> providerList = Provider.getAll(customerUUID);
    ArrayNode resultArray = Json.newArray();
    for (Provider provider : providerList) {
      List<Region> regionList = Region.fetchValidRegions(customerUUID, provider.uuid, 1);
      for (Region region : regionList) {
        ObjectNode regionNode = (ObjectNode) Json.toJson(region);
        ObjectNode providerForRegion = (ObjectNode) Json.toJson(provider);
        providerForRegion.remove("regions"); // to Avoid recursion
        regionNode.set("provider", providerForRegion);
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
  @ApiOperation(value = "Create a new region", response = Region.class, nickname = "createRegion")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "region",
          value = "region form data for new region to be created",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RegionFormData",
          required = true))
  public Result create(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Form<RegionFormData> formData = formFactory.getFormDataOrBadRequest(RegionFormData.class);
    RegionFormData form = formData.get();
    String regionCode = form.code;

    if (Region.getByCode(provider, regionCode) != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Region code already exists: " + regionCode);
    }

    Map<String, Object> regionMetadata =
        configHelper.getRegionMetadata(Common.CloudType.valueOf(provider.code));

    Region region;
    // If we have region metadata we create the region with that metadata or else we assume
    // some metadata is passed in (esp for onprem case).
    if (regionMetadata.containsKey(regionCode)) {
      JsonNode metaData = Json.toJson(regionMetadata.get(regionCode));
      region = Region.createWithMetadata(provider, regionCode, metaData);

      if (provider.code.equals("gcp")) {
        JsonNode zoneInfo = getZoneInfoOrFail(provider, region);
        // TODO(bogdan): change this and add test...
        List<String> zones = Json.fromJson(zoneInfo.get(regionCode).get("zones"), List.class);
        List<String> subnetworks =
            Json.fromJson(zoneInfo.get(regionCode).get("subnetworks"), List.class);
        if (subnetworks.size() != 1) {
          region.delete(); // don't really need this anymore due to @Transactional
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "Region Bootstrap failed. Invalid number of subnets for region " + regionCode);
        }
        String subnet = subnetworks.get(0);
        region.zones = new ArrayList<>();
        zones.forEach(
            zone -> region.zones.add(AvailabilityZone.createOrThrow(region, zone, zone, subnet)));
      } else {
        // TODO: Move this to commissioner framework, Bootstrap the region with VPC, subnet etc.
        // TODO(bogdan): is this even used???
        /*
        JsonNode vpcInfo = networkManager.bootstrap(
            region.uuid, null, form.hostVpcId, form.destVpcId, form.hostVpcRegion);
        */
        JsonNode vpcInfo = getVpcInfoOrFail(region);
        Map<String, String> zoneSubnets =
            Json.fromJson(vpcInfo.get(regionCode).get("zones"), Map.class);
        region.zones = new ArrayList<>();
        zoneSubnets.forEach(
            (zone, subnet) ->
                region.zones.add(AvailabilityZone.createOrThrow(region, zone, zone, subnet)));
      }
    } else {
      region =
          Region.create(
              provider, regionCode, form.name, form.ybImage, form.latitude, form.longitude);
    }
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return PlatformResults.withData(region);
  }

  /**
   * DELETE endpoint for deleting a existing Region.
   *
   * @param customerUUID Customer UUID
   * @param providerUUID Provider UUID
   * @param regionUUID Region UUID
   * @return JSON response on whether or not delete region was sucessful or not.
   */
  @ApiOperation(value = "Delete a region", response = Object.class, nickname = "deleteRegion")
  public Result delete(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    region.disableRegionAndZones();
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }

  // TODO: Use @Transactionally on controller method to get rid of region.delete()
  // TODO: Move this to CloudQueryHelper after getting rid of region.delete()
  private JsonNode getZoneInfoOrFail(Provider provider, Region region) {
    JsonNode zoneInfo =
        cloudQueryHelper.getZones(
            region.uuid, provider.getUnmaskedConfig().get("CUSTOM_GCE_NETWORK"));
    if (zoneInfo.has("error") || !zoneInfo.has(region.code)) {
      region.delete();
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Region Bootstrap failed. Unable to fetch zones for " + region.code);
    }
    return zoneInfo;
  }

  // TODO: Use @Transactionally on controller method to get rid of region.delete()
  // TODO: Move this to NetworkManager after getting rid of region.delete()
  private JsonNode getVpcInfoOrFail(Region region) {
    JsonNode vpcInfo = networkManager.bootstrap(region.uuid, null, null /* customPayload */);
    if (vpcInfo.has("error") || !vpcInfo.has(region.code)) {
      region.delete();
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Region Bootstrap failed.");
    }
    return vpcInfo;
  }
}
