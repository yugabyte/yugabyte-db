// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

import java.util.HashSet;
import java.util.List;
import java.util.Map;

public class CloudRegionSetup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudRegionSetup.class);

  public static class Params extends CloudBootstrap.Params {
    public String regionCode;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    String regionCode = taskParams().regionCode;
    if (Region.getByCode(getProvider(), regionCode) != null) {
      throw new RuntimeException("Region " +  regionCode + " already setup");
    }
    if (!regionMetadata.containsKey(regionCode)) {
      throw new RuntimeException("Region " + regionCode + " metadata not found");
    }
    JsonNode metaData = Json.toJson(regionMetadata.get(regionCode));
    Provider provider = getProvider();
    final Region region = Region.createWithMetadata(provider, regionCode, metaData);
    CloudQueryHelper queryHelper = Play.current().injector().instanceOf(CloudQueryHelper.class);
    JsonNode zoneInfo = null;

    switch (Common.CloudType.valueOf(provider.code)) {
      case aws:
        zoneInfo =  queryHelper.getZones(region.uuid, taskParams().destVpcId);
        if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
          region.delete();
          String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
          throw new RuntimeException(errMsg);
        }
        Map<String, String> zoneSubnets = Json.fromJson(zoneInfo.get(regionCode), Map.class);
        region.zones = new HashSet<>();
        zoneSubnets.forEach((zone, subnet) ->
            region.zones.add(AvailabilityZone.create(region, zone, zone, subnet)));
        break;
      case gcp:
        zoneInfo =  queryHelper.getZones(region.uuid, taskParams().destVpcId);
        if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
          region.delete();
          String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
          throw new RuntimeException(errMsg);
        }
        List<String> zones = Json.fromJson(zoneInfo.get(regionCode).get("zones"), List.class);
        List<String> subnetworks = Json.fromJson(zoneInfo.get(regionCode).get("subnetworks"), List.class);
        if (subnetworks.size() != 1) {
          region.delete();
          throw new RuntimeException(
              "Region Bootstrap failed. Invalid number of subnets for region " + regionCode);
        }
        String subnet = subnetworks.get(0);
        region.zones = new HashSet<>();
        zones.forEach(zone -> {
          region.zones.add(AvailabilityZone.create(region, zone, zone, subnet));
        });
        break;
      default:
        throw new RuntimeException("Cannot bootstrap region " + regionCode + " for provider " +
            provider.code + ".");
    }
  }
}
