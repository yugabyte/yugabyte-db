package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap.Params.PerRegionMetadata;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class CloudRegionHelper {

  private final CloudQueryHelper queryHelper;
  private final RuntimeConfGetter confGetter;
  private final ConfigHelper configHelper;

  @Inject
  public CloudRegionHelper(
      CloudQueryHelper queryHelper, ConfigHelper configHelper, RuntimeConfGetter confGetter) {
    this.queryHelper = queryHelper;
    this.configHelper = configHelper;
    this.confGetter = confGetter;
  }

  public Region createRegion(
      Provider provider,
      String regionCode,
      String destVpcId,
      PerRegionMetadata metadata,
      boolean isFirstTry) {
    Region existingRegion = Region.getByCode(provider, regionCode);
    if (existingRegion != null && isFirstTry) {
      throw new RuntimeException("Region " + regionCode + " already setup");
    }

    Map<String, Object> regionMetadata =
        configHelper.getRegionMetadata(CloudType.valueOf(provider.getCode()));

    if (!regionMetadata.containsKey(regionCode)
        && !provider.getCloudCode().equals(CloudType.onprem)) {
      throw new RuntimeException("Region " + regionCode + " metadata not found");
    }
    Region createdRegion = null;
    if (provider.getCloudCode().equals(CloudType.onprem)) {
      // Create the onprem region using the config provided.
      createdRegion =
          Region.create(
              provider,
              regionCode,
              metadata.regionName,
              metadata.customImageId,
              metadata.latitude,
              metadata.longitude);
    } else {
      JsonNode metaData = Json.toJson(regionMetadata.get(regionCode));
      if (!isFirstTry && existingRegion != null) {
        createdRegion = existingRegion;
      } else {
        createdRegion = Region.createWithMetadata(provider, regionCode, metaData);
      }
    }
    final Region region = createdRegion;
    String customImageId = metadata.customImageId;
    String architecture = metadata.architecture != null ? metadata.architecture.name() : null;
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching)) {
      if (customImageId != null && !customImageId.isEmpty()) {
        region.setYbImage(customImageId);
        region.update();
      } else {
        switch (CloudType.valueOf(provider.getCode())) {
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

      // Attempt to find architecture for AWS providers.
      if (provider.getCode().equals(CloudType.aws.toString())
          && (region.getArchitecture() == null
              || (customImageId != null && !customImageId.isEmpty()))) {
        String arch = queryHelper.getImageArchitecture(region);
        if (arch == null || arch.isEmpty()) {
          log.warn(
              "Could not get architecture for image {} in region {}.",
              region.getYbImage(),
              region.getCode());

        } else {
          try {
            // explicitly overriding arch name to maintain equivalent type of architecture.
            if (arch.equals("arm64")) {
              arch = Architecture.aarch64.name();
            }
            region.setArchitecture(Architecture.valueOf(arch));
            region.update();
          } catch (IllegalArgumentException e) {
            log.warn(
                "{} not a valid architecture. Skipping for region {}.", arch, region.getCode());
          }
        }
      }
    }
    String customSecurityGroupId = metadata.customSecurityGroupId;
    if (customSecurityGroupId != null && !customSecurityGroupId.isEmpty()) {
      region.setSecurityGroupId(customSecurityGroupId);
      region.update();
    }
    String instanceTemplate = metadata.instanceTemplate;
    if (instanceTemplate != null && !instanceTemplate.isEmpty()) {
      if (region.getProvider().getCloudCode().equals(CloudType.gcp)) {
        GCPRegionCloudInfo g = CloudInfoInterface.get(region);
        g.setInstanceTemplate(instanceTemplate);
      }
      region.update();
    }

    JsonNode zoneInfo;
    switch (CloudType.valueOf(provider.getCode())) {
      case aws:
        // Setup subnets.
        Map<String, String> zoneSubnets = metadata.azToSubnetIds;
        // If no custom mapping, then query from devops.
        if (zoneSubnets == null || zoneSubnets.size() == 0) {
          // Since it is AWS, we will only have a VPC per region, and not a global destVpcId.
          zoneInfo = queryHelper.getZones(region.getUuid(), metadata.vpcId);
          if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
            region.delete();
            String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
            throw new RuntimeException(errMsg);
          }
          zoneSubnets = Json.fromJson(zoneInfo.get(regionCode), Map.class);
        }

        // In case vpcID & securityGroup is missing, we will end up creating new VPC.
        if (StringUtils.isBlank(metadata.vpcId)
            && StringUtils.isBlank(metadata.customSecurityGroupId)) {
          AWSCloudInfo awsCloudInfo = CloudInfoInterface.get(provider);
          awsCloudInfo.setVpcType(CloudInfoInterface.VPCType.NEW);
          provider.getDetails().getCloudInfo().setAws(awsCloudInfo);
          provider.save();
        }

        region.setVnetName(metadata.vpcId);
        region.update();
        region.setZones(new ArrayList<>());
        Map<String, String> zoneSecondarySubnets = metadata.azToSecondarySubnetIds;
        // Secondary subnets were passed, which mean they should have a one to one mapping.
        // If not, throw an error.
        if (zoneSecondarySubnets != null && !zoneSecondarySubnets.isEmpty()) {
          zoneSubnets.forEach(
              (zone, subnet) -> {
                region
                    .getZones()
                    .add(
                        AvailabilityZone.getOrCreate(
                            region,
                            zone,
                            zone,
                            subnet,
                            Optional.ofNullable(zoneSecondarySubnets.get(zone))
                                .orElseThrow(
                                    () ->
                                        new RuntimeException(
                                            "Secondary subnets for all zones must be provided"))));
              });
        } else {
          zoneSubnets.forEach(
              (zone, subnet) ->
                  region.getZones().add(AvailabilityZone.getOrCreate(region, zone, zone, subnet)));
        }
        break;
      case azu:
        Map<String, String> zoneNets = metadata.azToSubnetIds;
        String vnet = metadata.vpcId;
        if (vnet == null || vnet.isEmpty()) {
          vnet = queryHelper.getVnetOrFail(region);
        }
        region.setVnetName(vnet);
        region.update();
        if (zoneNets == null || zoneNets.size() == 0) {
          zoneInfo = queryHelper.getZones(region.getUuid(), vnet);
          if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
            region.delete();
            String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
            throw new RuntimeException(errMsg);
          }
          zoneNets = Json.fromJson(zoneInfo.get(regionCode), Map.class);
        }
        region.setZones(new ArrayList<>());
        zoneNets.forEach(
            (zone, subnet) ->
                region.getZones().add(AvailabilityZone.getOrCreate(region, zone, zone, subnet)));
        break;
      case gcp:
        ObjectNode customPayload = Json.newObject();
        ObjectNode perRegionMetadata = Json.newObject();
        perRegionMetadata.set(regionCode, Json.toJson(metadata));
        customPayload.set("perRegionMetadata", perRegionMetadata);
        zoneInfo = queryHelper.getZones(region.getUuid(), destVpcId, Json.stringify(customPayload));
        if (zoneInfo.has("error") || !zoneInfo.has(regionCode)) {
          region.delete();
          String errMsg = "Region Bootstrap failed. Unable to fetch zones for " + regionCode;
          throw new RuntimeException(errMsg);
        }
        List<String> zones = Json.fromJson(zoneInfo.get(regionCode).get("zones"), List.class);
        String subnetId = metadata.subnetId;
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
        final String secondarySubnet = metadata.secondarySubnetId;
        region.setZones(new ArrayList<>());
        zones.forEach(
            zone ->
                region
                    .getZones()
                    .add(
                        AvailabilityZone.getOrCreate(region, zone, zone, subnet, secondarySubnet)));
        break;
      case onprem:
        region.setZones(new ArrayList<>());
        metadata.azList.forEach(
            zone ->
                region
                    .getZones()
                    .add(
                        AvailabilityZone.getOrCreate(
                            region, zone.getCode(), zone.getName(), null, null)));
        break;
      default:
        throw new RuntimeException(
            "Cannot bootstrap region " + regionCode + " for provider " + provider.getCode() + ".");
    }

    return region;
  }
}
