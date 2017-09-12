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

    switch (Common.CloudType.valueOf(provider.code)) {
      case aws:
        NetworkManager networkManager = Play.current().injector().instanceOf(NetworkManager.class);
        JsonNode vpcInfo = networkManager.bootstrap(region.uuid, taskParams().hostVpcId,
            taskParams().destVpcId);
        if (vpcInfo.has("error") || !vpcInfo.has(regionCode)) {
          // If network bootstrap failed, we will delete the newly created region.
          region.delete();
          throw new RuntimeException("Region Bootstrap failed for: " + regionCode);
        }
        Map<String, String> zoneSubnets = Json.fromJson(vpcInfo.get(regionCode).get("zones"),
            Map.class);
        region.zones = new HashSet<>();
        zoneSubnets.forEach((zone, subnet) ->
            region.zones.add(AvailabilityZone.create(region, zone, zone, subnet)));
        break;
      case gcp:
        CloudQueryHelper queryHelper = Play.current().injector().instanceOf(CloudQueryHelper.class);
        JsonNode zoneInfo = queryHelper.getZones(region);
        if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
          region.delete();
          String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
          throw new RuntimeException(errMsg);
        }
        List<String> zones = Json.fromJson(zoneInfo.get(regionCode), List.class);
        region.zones = new HashSet<>();
        zones.forEach(zone -> {
          region.zones.add(AvailabilityZone.create(region, zone, zone, "subnet-" + regionCode));
        });
        break;
      default:
        throw new RuntimeException("Cannot bootstrap region " + regionCode + " for provider " +
            provider.code + ".");
    }
  }
}
