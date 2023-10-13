// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.CloudRegionHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.forms.RegionEditFormData;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class RegionHandler {

  @Inject NetworkManager networkManager;

  @Inject CloudQueryHelper cloudQueryHelper;

  @Inject ConfigHelper configHelper;

  @Inject ProviderEditRestrictionManager providerEditRestrictionManager;

  @Inject AWSCloudImpl awsCloudImpl;

  @Inject CloudRegionHelper cloudRegionHelper;

  @Inject CloudProviderHelper CloudProviderHelper;

  @Inject ImageBundleUtil imageBundleUtil;

  @Deprecated
  public Region createRegion(UUID customerUUID, UUID providerUUID, RegionFormData form) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID, () -> doCreateRegion(customerUUID, providerUUID, form));
  }

  public Region createRegion(UUID customerUUID, UUID providerUUID, Region region) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID, () -> doCreateRegion(customerUUID, providerUUID, region));
  }

  @Deprecated
  public Region editRegion(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, RegionEditFormData form) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID, () -> doEditRegion(customerUUID, providerUUID, regionUUID, form));
  }

  public Region editRegion(UUID customerUUID, UUID providerUUID, UUID regionUUID, Region region) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID, () -> doEditRegion(customerUUID, providerUUID, region));
  }

  public Region doEditRegion(UUID customerUUID, UUID providerUUID, Region region) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Region existingRegion = Region.getOrBadRequest(customerUUID, providerUUID, region.getUuid());
    if (existingRegion.getNodeCount() > 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Modifying region %s details is not allowed for providers in use.",
              existingRegion.getCode()));
    }

    if (provider.getCloudCode() == CloudType.onprem) {
      existingRegion.setLatitude(region.getLatitude());
      existingRegion.setLongitude(region.getLongitude());
    }

    existingRegion.setDetails(region.getDetails());
    existingRegion.save();
    modifyImageBundlesIfRequired(provider, existingRegion);
    CloudProviderHelper.updateAZs(provider, provider, region, existingRegion);
    existingRegion.refresh();

    return existingRegion;
  }

  public Region doEditRegion(
      UUID customerUUID, UUID providerUUID, UUID regionUUID, RegionEditFormData form) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);

    region.setSecurityGroupId(form.securityGroupId);
    region.setVnetName(form.vnetName);
    region.setYbImage(form.ybImage);
    region.update();
    modifyImageBundlesIfRequired(provider, region);

    return region;
  }

  public Region deleteRegion(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    return providerEditRestrictionManager.tryEditProvider(
        providerUUID, () -> doDeleteRegion(customerUUID, providerUUID, regionUUID));
  }

  public Region doDeleteRegion(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    long nodeCount = region.getNodeCount();
    if (nodeCount > 0) {
      throw new PlatformServiceException(
          FORBIDDEN,
          String.format(
              "There %s %d node%s in this region",
              nodeCount > 1 ? "are" : "is", nodeCount, nodeCount > 1 ? "s" : ""));
    }
    region.disableRegionAndZones();
    if (provider.getCloudCode().equals(CloudType.aws)
        && provider.getAllAccessKeys() != null
        && provider.getAllAccessKeys().size() > 0) {
      String keyPairName = AccessKey.getLatestKey(provider.getUuid()).getKeyCode();
      log.info(
          String.format("Deleting keyPair %s from region %s", keyPairName, regionUUID.toString()));
      awsCloudImpl.deleteKeyPair(provider, region, keyPairName);
    }
    return region;
  }

  @Deprecated
  private Region doCreateRegion(UUID customerUUID, UUID providerUUID, RegionFormData form) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    String regionCode = form.code;

    if (Region.getByCode(provider, regionCode) != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Region code already exists: " + regionCode);
    }

    Map<String, Object> regionMetadata =
        configHelper.getRegionMetadata(Common.CloudType.valueOf(provider.getCode()));

    Region region;
    // If we have region metadata we create the region with that metadata or else we assume
    // some metadata is passed in (esp for onprem case).
    if (regionMetadata.containsKey(regionCode)) {
      JsonNode metaData = Json.toJson(regionMetadata.get(regionCode));
      region = Region.createWithMetadata(provider, regionCode, metaData);

      if (provider.getCode().equals("gcp")) {
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
        region.setZones(new ArrayList<>());
        zones.forEach(
            zone ->
                region.getZones().add(AvailabilityZone.createOrThrow(region, zone, zone, subnet)));
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
        region.setZones(new ArrayList<>());
        zoneSubnets.forEach(
            (zone, subnet) ->
                region.getZones().add(AvailabilityZone.createOrThrow(region, zone, zone, subnet)));
      }
    } else {
      region =
          Region.create(
              provider, regionCode, form.name, form.ybImage, form.latitude, form.longitude);
    }
    modifyImageBundlesIfRequired(provider, region);
    return region;
  }

  private Region doCreateRegion(UUID customerUUID, UUID providerUUID, Region region) {
    // k8s/non-k8s region handling.
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    if (provider.getCloudCode() != CloudType.kubernetes) {
      CloudBootstrap.Params.PerRegionMetadata metadata =
          CloudBootstrap.Params.PerRegionMetadata.fromRegion(region);

      String destVpcId = null;
      if (provider.getCloudCode() == CloudType.gcp) {
        GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
        destVpcId = gcpCloudInfo.getDestVpcId();
      }

      region =
          cloudRegionHelper.createRegion(provider, region.getCode(), destVpcId, metadata, true);
      modifyImageBundlesIfRequired(provider, region);
    } else {
      // Handle k8s region bootstrap.
      Set<Region> regions = new HashSet<>();
      regions.add(region);
      CloudProviderHelper.editKubernetesProvider(provider, provider, regions);
      region = Region.getByCode(provider, region.getCode());
    }

    return region;
  }

  // TODO: Use @Transactionally on controller method to get rid of region.delete()
  // TODO: Move this to CloudQueryHelper after getting rid of region.delete()
  private JsonNode getZoneInfoOrFail(Provider provider, Region region) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    String gcpNetwork = gcpCloudInfo.getDestVpcId();
    JsonNode zoneInfo = cloudQueryHelper.getZones(region.getUuid(), gcpNetwork);
    if (zoneInfo.has("error") || !zoneInfo.has(region.getCode())) {
      region.delete();
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Region Bootstrap failed. Unable to fetch zones for " + region.getCode());
    }
    return zoneInfo;
  }

  // TODO: Use @Transactionally on controller method to get rid of region.delete()
  // TODO: Move this to NetworkManager after getting rid of region.delete()
  private JsonNode getVpcInfoOrFail(Region region) {
    JsonNode vpcInfo = networkManager.bootstrap(region.getUuid(), null, null /* customPayload */);
    if (vpcInfo.has("error") || !vpcInfo.has(region.getCode())) {
      region.delete();
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Region Bootstrap failed.");
    }
    return vpcInfo;
  }

  private void modifyImageBundlesIfRequired(Provider provider, Region region) {
    List<Region> regions = new ArrayList<>();
    regions.add(region);
    provider
        .getImageBundles()
        .forEach(
            bundle -> {
              imageBundleUtil.updateImageBundleIfRequired(provider, regions, bundle);
            });
  }
}
