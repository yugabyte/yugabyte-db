/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.api.Play;
import play.libs.Json;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CloudRegionSetup extends CloudTaskBase {

  @Inject
  protected CloudRegionSetup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends CloudTaskParams {
    public String regionCode;
    public CloudBootstrap.Params.PerRegionMetadata metadata;
    public String destVpcId;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    CloudQueryHelper queryHelper = Play.current().injector().instanceOf(CloudQueryHelper.class);
    String regionCode = taskParams().regionCode;
    Provider provider = getProvider();
    if (Region.getByCode(getProvider(), regionCode) != null) {
      throw new RuntimeException("Region " + regionCode + " already setup");
    }
    if (!regionMetadata.containsKey(regionCode)
        && !provider.getCloudCode().equals(Common.CloudType.onprem)) {
      throw new RuntimeException("Region " + regionCode + " metadata not found");
    }
    Region createdRegion = null;
    if (provider.getCloudCode().equals(Common.CloudType.onprem)) {
      // Create the onprem region using the config provided.
      createdRegion =
          Region.create(
              provider,
              regionCode,
              taskParams().metadata.regionName,
              taskParams().metadata.customImageId,
              taskParams().metadata.latitude,
              taskParams().metadata.longitude);
    } else {
      JsonNode metaData = Json.toJson(regionMetadata.get(regionCode));
      createdRegion = Region.createWithMetadata(provider, regionCode, metaData);
    }
    final Region region = createdRegion;
    String customImageId = taskParams().metadata.customImageId;
    String architecture =
        taskParams().metadata.architecture != null
            ? taskParams().metadata.architecture.name()
            : null;
    if (customImageId != null && !customImageId.isEmpty()) {
      region.setYbImage(customImageId);
      region.update();
    } else {
      switch (Common.CloudType.valueOf(provider.code)) {
          // Intentional fallthrough for AWS, Azure & GCP should be covered the same way.
        case aws:
        case gcp:
        case azu:
          // Setup default image, if no custom one was specified.
          String defaultImage = queryHelper.getDefaultImage(region, architecture);
          if (defaultImage == null || defaultImage.isEmpty()) {
            throw new RuntimeException("Could not get default image for region: " + regionCode);
          }
          region.setYbImage(defaultImage);
          region.update();
          break;
      }
    }
    String customSecurityGroupId = taskParams().metadata.customSecurityGroupId;
    if (customSecurityGroupId != null && !customSecurityGroupId.isEmpty()) {
      region.setSecurityGroupId(customSecurityGroupId);
      region.update();
    }
    String instanceTemplate = taskParams().metadata.instanceTemplate;
    if (instanceTemplate != null && !instanceTemplate.isEmpty()) {
      if (region.provider.getCloudCode().equals(Common.CloudType.gcp)) {
        GCPRegionCloudInfo g = CloudInfoInterface.get(region);
        g.setInstanceTemplate(instanceTemplate);
      }
      region.update();
    }

    // Attempt to find architecture for AWS providers.
    if (provider.code.equals(Common.CloudType.aws.toString())
        && (region.getArchitecture() == null
            || (customImageId != null && !customImageId.isEmpty()))) {
      String arch = queryHelper.getImageArchitecture(region);
      if (arch == null || arch.isEmpty()) {
        log.warn(
            "Could not get architecture for image {} in region {}.",
            region.getYbImage(),
            region.code);

      } else {
        try {
          // explicitly overriding arch name to maintain equivalent type of architecture.
          if (arch.equals("arm64")) {
            arch = Architecture.aarch64.name();
          }
          region.setArchitecture(Architecture.valueOf(arch));
          region.update();
        } catch (IllegalArgumentException e) {
          log.warn("{} not a valid architecture. Skipping for region {}.", arch, region.code);
        }
      }
    }

    JsonNode zoneInfo;
    switch (Common.CloudType.valueOf(provider.code)) {
      case aws:
        // Setup subnets.
        Map<String, String> zoneSubnets = taskParams().metadata.azToSubnetIds;
        // If no custom mapping, then query from devops.
        if (zoneSubnets == null || zoneSubnets.size() == 0) {
          // Since it is AWS, we will only have a VPC per region, and not a global destVpcId.
          zoneInfo = queryHelper.getZones(region.uuid, taskParams().metadata.vpcId);
          if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
            region.delete();
            String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
            throw new RuntimeException(errMsg);
          }
          zoneSubnets = Json.fromJson(zoneInfo.get(regionCode), Map.class);
        }

        // In case vpcID & securityGroup is missing, we will end up creating new VPC.
        if (StringUtils.isBlank(taskParams().metadata.vpcId)
            && StringUtils.isBlank(taskParams().metadata.customSecurityGroupId)) {
          AWSCloudInfo awsCloudInfo = CloudInfoInterface.get(provider);
          awsCloudInfo.setVpcType(CloudInfoInterface.VPCType.NEW);
          provider.details.cloudInfo.setAws(awsCloudInfo);
          provider.save();
        }

        region.setVnetName(taskParams().metadata.vpcId);
        region.update();
        region.zones = new ArrayList<>();
        Map<String, String> zoneSecondarySubnets = taskParams().metadata.azToSecondarySubnetIds;
        // Secondary subnets were passed, which mean they should have a one to one mapping.
        // If not, throw an error.
        if (zoneSecondarySubnets != null && !zoneSecondarySubnets.isEmpty()) {
          zoneSubnets.forEach(
              (zone, subnet) ->
                  region.zones.add(
                      AvailabilityZone.createOrThrow(
                          region,
                          zone,
                          zone,
                          subnet,
                          Optional.ofNullable(zoneSecondarySubnets.get(zone))
                              .orElseThrow(
                                  () ->
                                      new RuntimeException(
                                          "Secondary subnets for all zones must be provided")))));
        } else {
          zoneSubnets.forEach(
              (zone, subnet) ->
                  region.zones.add(AvailabilityZone.createOrThrow(region, zone, zone, subnet)));
        }
        break;
      case azu:
        Map<String, String> zoneNets = taskParams().metadata.azToSubnetIds;
        String vnet = taskParams().metadata.vpcId;
        if (vnet == null || vnet.isEmpty()) {
          vnet = queryHelper.getVnetOrFail(region);
        }
        region.setVnetName(vnet);
        region.update();
        if (zoneNets == null || zoneNets.size() == 0) {
          zoneInfo = queryHelper.getZones(region.uuid, vnet);
          if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
            region.delete();
            String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
            throw new RuntimeException(errMsg);
          }
          zoneNets = Json.fromJson(zoneInfo.get(regionCode), Map.class);
        }
        region.zones = new ArrayList<>();
        zoneNets.forEach(
            (zone, subnet) ->
                region.zones.add(AvailabilityZone.createOrThrow(region, zone, zone, subnet)));
        break;
      case gcp:
        ObjectNode customPayload = Json.newObject();
        ObjectNode perRegionMetadata = Json.newObject();
        perRegionMetadata.set(regionCode, Json.toJson(taskParams().metadata));
        customPayload.set("perRegionMetadata", perRegionMetadata);
        zoneInfo =
            queryHelper.getZones(
                region.uuid, taskParams().destVpcId, Json.stringify(customPayload));
        if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
          region.delete();
          String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
          throw new RuntimeException(errMsg);
        }
        List<String> zones = Json.fromJson(zoneInfo.get(regionCode).get("zones"), List.class);
        String subnetId = taskParams().metadata.subnetId;
        if (subnetId == null || subnetId.isEmpty()) {
          List<String> subnetworks =
              Json.fromJson(zoneInfo.get(regionCode).get("subnetworks"), List.class);
          if (subnetworks.size() != 1) {
            region.delete();
            throw new RuntimeException(
                "Region Bootstrap failed. Invalid number of subnets for region " + regionCode);
          }
          subnetId = subnetworks.get(0);
        }
        final String subnet = subnetId;
        // Will be null in case not provided.
        final String secondarySubnet = taskParams().metadata.secondarySubnetId;
        region.zones = new ArrayList<>();
        zones.forEach(
            zone ->
                region.zones.add(
                    AvailabilityZone.createOrThrow(region, zone, zone, subnet, secondarySubnet)));
        break;
      case onprem:
        region.zones = new ArrayList<>();
        taskParams()
            .metadata
            .azList
            .forEach(
                zone ->
                    region.zones.add(
                        AvailabilityZone.createOrThrow(region, zone.code, zone.name, null, null)));
        break;
      default:
        throw new RuntimeException(
            "Cannot bootstrap region " + regionCode + " for provider " + provider.code + ".");
    }
  }
}
