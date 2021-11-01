/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Singleton
public class CloudQueryHelper extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudQueryHelper.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "query";
  private static final String DEFAULT_IMAGE_KEY = "default_image";

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  public JsonNode currentHostInfo(Common.CloudType cloudType, List<String> metadataTypes) {
    List<String> commandArgs = new ArrayList<>();
    if (metadataTypes != null) {
      commandArgs.add("--metadata_types");
      commandArgs.addAll(metadataTypes);
    }
    return parseShellResponse(
        execCommand(null, null, cloudType, "current-host", commandArgs, new ArrayList<>()),
        "current-host");
  }

  public List<String> getRegionCodes(Provider p) {
    List<String> commandArgs = new ArrayList<>();
    if (p.code.equals("gcp")) {
      // TODO: ideally we shouldn't have this hardcoded string present in multiple places.
      String potentialGcpNetwork = p.getUnmaskedConfig().get("CUSTOM_GCE_NETWORK");
      if (potentialGcpNetwork != null && !potentialGcpNetwork.isEmpty()) {
        commandArgs.add("--network");
        commandArgs.add(potentialGcpNetwork);
      }
    }
    JsonNode regionInfo = execAndParseCommandCloud(p.uuid, "regions", commandArgs);
    List<String> regionCodes = ImmutableList.of();
    if (regionInfo instanceof ArrayNode) {
      regionCodes = Json.fromJson(regionInfo, List.class);
    }
    return regionCodes;
  }

  public JsonNode getZones(UUID regionUUID) {
    return getZones(regionUUID, null);
  }

  public JsonNode getZones(UUID regionUUID, String destVpcId) {
    return getZones(regionUUID, destVpcId, null);
  }

  public JsonNode getZones(UUID regionUUID, String destVpcId, String customPayload) {
    Region region = Region.get(regionUUID);
    List<String> commandArgs = new ArrayList<>();
    if (destVpcId != null && !destVpcId.isEmpty()) {
      commandArgs.add("--dest_vpc_id");
      commandArgs.add(destVpcId);
    }
    if (customPayload != null && !customPayload.isEmpty()) {
      commandArgs.add("--custom_payload");
      commandArgs.add(customPayload);
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
  public JsonNode getInstanceTypes(List<Region> regionList, String customPayload) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--regions");
    regionList.forEach(region -> commandArgs.add(region.code));
    if (customPayload != null && !customPayload.isEmpty()) {
      commandArgs.add("--custom_payload");
      commandArgs.add(customPayload);
    }
    if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.internal.gcp_instances")
        && regionList.get(0).provider.code.equals("gcp")) {
      commandArgs.add("--gcp_internal");
    }
    return execAndParseCommandRegion(regionList.get(0).uuid, "instance_types", commandArgs);
  }

  public JsonNode getMachineImages(UUID providerUUID, Region region) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--regions");
    commandArgs.add(region.code);
    return execAndParseCommandCloud(providerUUID, "ami", commandArgs);
  }

  public JsonNode queryVpcs(UUID regionUUID) {
    List<String> commandArgs = new ArrayList<>();
    return execAndParseCommandRegion(regionUUID, "vpc", commandArgs);
  }

  public String getDefaultImage(Region region) {
    String defaultImage = null;

    JsonNode result = queryVpcs(region.uuid);

    JsonNode regionInfo = result.get(region.code);
    if (regionInfo != null) {
      JsonNode defaultImageJson = regionInfo.get(DEFAULT_IMAGE_KEY);
      if (defaultImageJson != null) {
        defaultImage = defaultImageJson.asText();
      }
    }
    return defaultImage;
  }

  public JsonNode queryVnet(UUID regionUUID) {
    List<String> commandArgs = new ArrayList<>();
    return execAndParseCommandRegion(regionUUID, "vnet", commandArgs);
  }

  public String getVnetOrFail(Region region) {
    JsonNode result = queryVnet(region.uuid);

    JsonNode regionVnet = result.get(region.code);
    if (regionVnet == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not get vnet for region: " + region.code);
    }
    return regionVnet.asText();
  }
}
