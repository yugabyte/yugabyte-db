// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Singleton
public class CloudQueryHelper extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudQueryHelper.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "query";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode currentHostInfo(Common.CloudType cloudType, List<String> metadataTypes) {
    List<String> commandArgs = new ArrayList<String>();
    if (metadataTypes != null) {
      commandArgs.add("--metadata_types");
      commandArgs.addAll(metadataTypes);
    }
    return parseShellResponse(
        execCommand(null, null, cloudType, "current-host", commandArgs, new ArrayList<String>()),
        "current-host");
  }

  public JsonNode getRegions(UUID providerUUID) {
    Provider p = Provider.get(providerUUID);
    List<String> commandArgs = new ArrayList<String>();
    if (p.code.equals("gcp")) {
      // TODO: ideally we shouldn't have this hardcoded string present in multiple places.
      String potentialGcpNetwork = p.getConfig().get("CUSTOM_GCE_NETWORK");
      if (potentialGcpNetwork != null && !potentialGcpNetwork.isEmpty()) {
        commandArgs.add("--network");
        commandArgs.add(potentialGcpNetwork);
      }
    }
    return execAndParseCommandCloud(providerUUID, "regions", commandArgs);
  }

  public JsonNode getZones(Region region) {
    return getZones(region, null);
  }

  public JsonNode getZones(Region region, String destVpcId) {
    List<String> commandArgs = new ArrayList<String>();
    commandArgs.add("--regions");
    commandArgs.add(region.code);
    if (destVpcId != null && !destVpcId.isEmpty()) {
      commandArgs.add("--dest_vpc_id");
      commandArgs.add(destVpcId);
    }
    return execAndParseCommandRegion(region.uuid, "zones", commandArgs);
  }

  /*
   * Example return from GCP:
   * {
   *   "n1-standard-32": {
   *     "prices": {
   *         "us-west1": [
   *         {
   *           "price": 1.52,
   *           "os": "Linux"
   *         }
   *       ],
   *       "us-east1": [
   *         {
   *           "price": 1.52,
   *           "os": "Linux"
   *         }
   *       ]
   *     },
   *     "numCores": 32,
   *     "description": "32 vCPUs, 120 GB RAM",
   *     "memSizeGb": 120,
   *     "isShared": false
   *   },
   *   ...
   * }
   */
  public JsonNode getInstanceTypes(List<Region> regionList) {
    List<String> commandArgs = new ArrayList<String>();
    commandArgs.add("--regions");
    regionList.forEach(region -> commandArgs.add(region.code));
    return execAndParseCommandRegion(
        regionList.get(0).uuid, "instance_types", commandArgs);
  }

  /**
   * Get a suggested spot price for a given list of regions. Will find the max spot price amongst all the regions and
   * return a suggested spot price of double the max spot price found.
   *
   * @param regions Regions to get the suggested spot price for.
   * @param instanceType Instance type to get the suggested spot price for.
   * @return Double value which is the suggested spot price for a given instance type over all regions.
   */
  public double getSuggestedSpotPrice(List<Region> regions, String instanceType) {
    String command = "spot-pricing";
    double maxPriceFound = 0.0;
    for (Region region : regions) {
      for (AvailabilityZone availabilityZone : AvailabilityZone.getAZsForRegion(region.uuid)) {
        List<String> cloudArgs = ImmutableList.of("--zone", availabilityZone.code);
        List<String> commandArgs = ImmutableList.of("--instance_type", instanceType);
        JsonNode result = parseShellResponse(execCommand(region.uuid, null, null, command, commandArgs, cloudArgs), command);
        if (result.has("error")) {
          throw new RuntimeException(result.get("error").asText());
        }
        double price = result.get("SpotPrice").asDouble();
        if (price > maxPriceFound) maxPriceFound = price;
      }
    }
    return 2.0 * maxPriceFound;
  }
}
