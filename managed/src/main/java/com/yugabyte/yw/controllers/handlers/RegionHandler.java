// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.forms.RegionEditFormData;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Singleton;
import play.libs.Json;

@Singleton
public class RegionHandler {

  @Inject NetworkManager networkManager;

  @Inject CloudQueryHelper cloudQueryHelper;

  @Inject ConfigHelper configHelper;

  @Inject ProviderEditRestrictionManager providerEditRestrictionManager;

  // TODO: this will be removed after UI revamp
  public Region createRegion(UUID customerUUID, UUID providerUUID, RegionFormData form) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID, () -> doCreateRegion(customerUUID, providerUUID, form));
  }

  public Region editRegion(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, RegionEditFormData form) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);

          region.setSecurityGroupId(form.securityGroupId);
          region.setVnetName(form.vnetName);
          region.setYbImage(form.ybImage);

          region.update();
          return region;
        });
  }

  public Region deleteRegion(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
          long nodeCount = region.getNodeCount();
          if (nodeCount > 0) {
            throw new PlatformServiceException(
                FORBIDDEN,
                String.format(
                    "There %s %d node%s in this region",
                    nodeCount > 1 ? "are" : "is", nodeCount, nodeCount > 1 ? "s" : ""));
          }
          region.disableRegionAndZones();
          return region;
        });
  }

  private Region doCreateRegion(UUID customerUUID, UUID providerUUID, RegionFormData form) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
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

    return region;
  }

  // TODO: Use @Transactionally on controller method to get rid of region.delete()
  // TODO: Move this to CloudQueryHelper after getting rid of region.delete()
  private JsonNode getZoneInfoOrFail(Provider provider, Region region) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    String gcpNetwork = gcpCloudInfo.getDestVpcId();
    JsonNode zoneInfo = cloudQueryHelper.getZones(region.uuid, gcpNetwork);
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
