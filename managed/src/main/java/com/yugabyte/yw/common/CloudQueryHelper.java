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
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Singleton
public class CloudQueryHelper extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudQueryHelper.class);
  private static final List<String> AWS_METADATA_TYPES =
      ImmutableList.of("instance-id", "vpc-id", "privateIp", "region");

  private static final String YB_CLOUD_COMMAND_TYPE = "query";
  private static final String DEFAULT_IMAGE_KEY = "default_image";
  public static final String ARCHITECTURE_KEY = "architecture";

  private Map<Common.CloudType, JsonNode> cachedHostInfos = new ConcurrentHashMap<>();

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  public JsonNode getCurrentHostInfo(Common.CloudType cloudType) {
    return cachedHostInfos.computeIfAbsent(
        cloudType,
        ct -> currentHostInfo(ct, ct == Common.CloudType.aws ? AWS_METADATA_TYPES : null));
  }

  private JsonNode currentHostInfo(
      Common.CloudType cloudType, @Nullable List<String> metadataTypes) {
    List<String> commandArgs = new ArrayList<>();
    if (metadataTypes != null) {
      commandArgs.add("--metadata_types");
      commandArgs.addAll(metadataTypes);
    }
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .cloudType(cloudType)
            .command("current-host")
            .commandArgs(commandArgs)
            .build());
  }

  public List<String> getRegionCodes(Provider p) {
    List<String> commandArgs = new ArrayList<>();
    if (p.getCode().equals("gcp")) {
      // TODO: ideally we shouldn't have this hardcoded string present in multiple
      // places.
      GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(p);
      String potentialGcpNetwork = gcpCloudInfo.getDestVpcId();
      if (potentialGcpNetwork != null && !potentialGcpNetwork.isEmpty()) {
        commandArgs.add("--network");
        commandArgs.add(potentialGcpNetwork);
      }
    }
    JsonNode regionInfo =
        execAndParseShellResponse(
            DevopsCommand.builder()
                .providerUUID(p.getUuid())
                .command("regions")
                .commandArgs(commandArgs)
                .build());
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
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .regionUUID(region.getUuid())
            .command("zones")
            .commandArgs(commandArgs)
            .build());
  }

  /*
   * Example return from GCP:
   * {
   * "n1-standard-32": {
   * "prices": {
   * "us-west1": [
   * {
   * "price": 1.52,
   * "os": "Linux"
   * }
   * ],
   * "us-east1": [
   * {
   * "price": 1.52,
   * "os": "Linux"
   * }
   * ]
   * },
   * "numCores": 32,
   * "description": "32 vCPUs, 120 GB RAM",
   * "memSizeGb": 120,
   * "isShared": false
   * },
   * ...
   * }
   */
  public JsonNode getInstanceTypes(List<Region> regionList, String customPayload) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--regions");
    regionList.forEach(region -> commandArgs.add(region.getCode()));
    if (customPayload != null && !customPayload.isEmpty()) {
      commandArgs.add("--custom_payload");
      commandArgs.add(customPayload);
    }
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .regionUUID(regionList.get(0).getUuid())
            .command("instance_types")
            .commandArgs(commandArgs)
            .build());
  }

  public JsonNode getMachineImages(UUID providerUUID, Region region) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--regions");
    commandArgs.add(region.getCode());
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .providerUUID(providerUUID)
            .command("ami")
            .commandArgs(commandArgs)
            .build());
  }

  public JsonNode queryVpcs(UUID regionUUID, String arch) {
    List<String> commandArgs = new ArrayList<>();
    if (arch != null) {
      commandArgs.add("--architecture");
      commandArgs.add(arch);
    }
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .regionUUID(regionUUID)
            .command("vpc")
            .commandArgs(commandArgs)
            .build());
  }

  public String getDefaultImage(Region region, String architecture) {
    String defaultImage = null;
    JsonNode result = queryVpcs(region.getUuid(), architecture);

    JsonNode regionInfo = result.get(region.getCode());
    if (regionInfo != null) {
      JsonNode defaultImageJson = regionInfo.get(DEFAULT_IMAGE_KEY);
      if (defaultImageJson != null) {
        defaultImage = defaultImageJson.asText();
      }
    }
    return defaultImage;
  }

  public JsonNode queryImage(UUID regionUUID, String ybImage) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--machine_image");
    commandArgs.add(ybImage);
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .regionUUID(regionUUID)
            .command("image")
            .commandArgs(commandArgs)
            .build());
  }

  public String getImageArchitecture(Region region) {

    if (StringUtils.isBlank(region.getYbImage())) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "ybImage not set for region " + region.getCode());
    }
    JsonNode result = queryImage(region.getUuid(), region.getYbImage());

    if (result.has("error")) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error querying image architecture " + result.get("error"));
    }

    String arch = null;
    JsonNode archJson = result.get(ARCHITECTURE_KEY);
    if (archJson != null) {
      arch = archJson.asText();
    }
    return arch;
  }

  public JsonNode queryVnet(UUID regionUUID) {
    List<String> commandArgs = new ArrayList<>();
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .regionUUID(regionUUID)
            .command("vnet")
            .commandArgs(commandArgs)
            .build());
  }

  public String getVnetOrFail(Region region) {
    JsonNode result = queryVnet(region.getUuid());

    JsonNode regionVnet = result.get(region.getCode());
    if (regionVnet == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not get vnet for region: " + region.getCode());
    }
    return regionVnet.asText();
  }
}
