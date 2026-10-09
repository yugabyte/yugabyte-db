/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud.oci;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.AbstractInitializer;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.PriceComponent.PriceDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.io.IOException;
import java.io.InputStream;
import java.time.Instant;
import java.time.format.DateTimeFormatter;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.Environment;
import play.libs.Json;

@Slf4j
@Singleton
public class OCIInitializer extends AbstractInitializer {

  private static final int FALLBACK_NVME_SIZE_GB = 6800;

  @Inject private ConfigHelper configHelper;
  @Inject private Environment environment;

  /**
   * Entry point to initialize OCI. Loads instance types (YAML + live shapes) and upserts bundled
   * compute/block meter PriceComponents per region.
   */
  @Override
  public void initialize(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);

    log.info("Loading OCI instance types from YAML metadata for provider {}", providerUUID);
    initializeFromYamlMetadata(provider);

    List<Region> regionList = Region.fetchValidRegions(customerUUID, providerUUID, 0);
    // Bundled pricing must not depend on OCI API availability (unlike shape discovery).
    try {
      loadInstanceTypesFromApi(provider, regionList);
    } catch (Exception e) {
      log.warn(
          "Failed to fetch OCI instance types from API for provider {}; continuing with bundled"
              + " pricing. Error: {}",
          providerUUID,
          e.getMessage());
    }
    storeBundledPriceComponents(provider, regionList);
  }

  private void loadInstanceTypesFromApi(Provider provider, List<Region> regionList) {
    JsonNode instanceTypes =
        getCloudQueryHelper()
            .getInstanceTypes(regionList, Json.stringify(Json.toJson(provider.getCloudParams())));

    if (instanceTypes == null || instanceTypes.isEmpty()) {
      log.info(
          "No additional instance types returned from OCI API for provider {}", provider.getUuid());
      return;
    }

    log.info(
        "Adding {} instance types from OCI API for provider {}",
        instanceTypes.size(),
        provider.getUuid());

    Iterator<String> itr = instanceTypes.fieldNames();
    while (itr.hasNext()) {
      String instanceTypeCode = itr.next();
      // ListShapes reports Flex defaults (1 OCPU / 16 GB). YAML owns the valid min SKU.
      if (isLocalNvmeFlex(instanceTypeCode)) {
        continue;
      }
      JsonNode instanceTypeToDetailsMap = instanceTypes.get(instanceTypeCode);

      InstanceType existing = InstanceType.get(provider.getUuid(), instanceTypeCode);
      double numCores =
          resourceFromApi(instanceTypeCode, instanceTypeToDetailsMap, existing, "numCores");
      double memSizeGb =
          resourceFromApi(instanceTypeCode, instanceTypeToDetailsMap, existing, "memSizeGb");
      InstanceTypeDetails instanceTypeDetails =
          detailsFromApi(instanceTypeCode, instanceTypeToDetailsMap, existing);

      InstanceType.upsert(
          provider.getUuid(), instanceTypeCode, numCores, memSizeGb, instanceTypeDetails);
    }
  }

  private void storeBundledPriceComponents(Provider provider, List<Region> regionList) {
    if (regionList == null || regionList.isEmpty()) {
      log.warn("No regions available to store OCI pricing for provider {}", provider.getUuid());
      return;
    }

    JsonNode pricelist = loadPricelist();
    JsonNode computeFamilies = pricelist.get("computeFamilies");
    JsonNode blockVolume = pricelist.get("blockVolume");
    if (computeFamilies == null || !computeFamilies.isObject()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "OCI pricelist missing computeFamilies");
    }
    if (blockVolume == null
        || !blockVolume.has("storageGbPerMonth")
        || !blockVolume.has("vpuPerGbPerMonth")) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "OCI pricelist missing blockVolume meters");
    }

    String now = DateTimeFormatter.ISO_INSTANT.format(Instant.now());
    for (Region region : regionList) {
      Iterator<String> familyItr = computeFamilies.fieldNames();
      while (familyItr.hasNext()) {
        String family = familyItr.next();
        JsonNode rates = computeFamilies.get(family);
        upsertHourlyMeter(
            provider.getUuid(),
            region.getCode(),
            OCIPriceUtil.ocpuComponentCode(family),
            rates.path("ocpuPerHour").asDouble(0.0),
            now);
        upsertHourlyMeter(
            provider.getUuid(),
            region.getCode(),
            OCIPriceUtil.memoryComponentCode(family),
            rates.path("memoryGbPerHour").asDouble(0.0),
            now);
      }

      double storageHourly =
          OCIPriceUtil.monthlyToHourly(blockVolume.get("storageGbPerMonth").asDouble());
      double vpuHourly =
          OCIPriceUtil.monthlyToHourly(blockVolume.get("vpuPerGbPerMonth").asDouble());
      upsertHourlyMeter(
          provider.getUuid(),
          region.getCode(),
          OCIPriceUtil.blockStorageComponentCode(),
          storageHourly,
          now);
      upsertHourlyMeter(
          provider.getUuid(),
          region.getCode(),
          OCIPriceUtil.blockVpuComponentCode(),
          vpuHourly,
          now);
    }

    log.info(
        "Stored OCI bundled pricing meters for {} regions on provider {}",
        regionList.size(),
        provider.getUuid());
  }

  private JsonNode loadPricelist() {
    try (InputStream pricingStream =
        environment.resourceAsStream(OCIPriceUtil.PRICELIST_RESOURCE)) {
      if (pricingStream == null) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Missing bundled OCI pricing file " + OCIPriceUtil.PRICELIST_RESOURCE);
      }
      return new ObjectMapper().readTree(pricingStream);
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to parse OCI pricing file: " + e.getMessage());
    }
  }

  private static void upsertHourlyMeter(
      UUID providerUuid, String regionCode, String componentCode, double pricePerHour, String now) {
    PriceDetails priceDetails = new PriceDetails();
    priceDetails.unit = PriceDetails.Unit.Hours;
    priceDetails.pricePerUnit = pricePerHour;
    priceDetails.pricePerHour = pricePerHour;
    priceDetails.pricePerDay = pricePerHour * 24.0;
    priceDetails.pricePerMonth = priceDetails.pricePerDay * 30.0;
    priceDetails.currency = PriceDetails.Currency.USD;
    priceDetails.effectiveDate = now;
    PriceComponent.upsert(providerUuid, regionCode, componentCode, priceDetails);
  }

  /**
   * Fallback method to load OCI instance types from the YAML metadata file when OCI API returns no
   * results. This ensures basic instance types are available for universe creation.
   */
  private void initializeFromYamlMetadata(Provider provider) {
    Map<String, Object> ociInstanceTypeMetadata =
        configHelper.getConfig(ConfigType.OCIInstanceTypeMetadata);

    if (ociInstanceTypeMetadata == null || ociInstanceTypeMetadata.isEmpty()) {
      log.warn(
          "No OCI instance type metadata found in YAML config for provider {}", provider.getUuid());
      return;
    }

    log.info(
        "Loading {} OCI instance types from YAML metadata for provider {}",
        ociInstanceTypeMetadata.size(),
        provider.getUuid());

    for (Map.Entry<String, Object> entry : ociInstanceTypeMetadata.entrySet()) {
      String instanceTypeCode = entry.getKey();
      @SuppressWarnings("unchecked")
      Map<String, Object> details = (Map<String, Object>) entry.getValue();

      int numCores = 0;
      double memSizeGb = 0;

      if (details.containsKey("numCores")) {
        numCores = ((Number) details.get("numCores")).intValue();
      }
      if (details.containsKey("memSizeGB")) {
        memSizeGb = ((Number) details.get("memSizeGB")).doubleValue();
      }

      InstanceTypeDetails instanceTypeDetails = InstanceTypeDetails.createOCIDefault();

      if (details.containsKey("instanceTypeDetails")) {
        @SuppressWarnings("unchecked")
        Map<String, Object> detailsMap = (Map<String, Object>) details.get("instanceTypeDetails");
        if (detailsMap.containsKey("volumeDetailsList")) {
          @SuppressWarnings("unchecked")
          List<Map<String, Object>> volumeList =
              (List<Map<String, Object>>) detailsMap.get("volumeDetailsList");
          instanceTypeDetails.volumeDetailsList.clear();
          for (Map<String, Object> volume : volumeList) {
            InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
            volumeDetails.volumeSizeGB = ((Number) volume.get("volumeSizeGB")).intValue();
            String volumeType = (String) volume.get("volumeType");
            volumeDetails.volumeType = InstanceType.VolumeType.valueOf(volumeType);
            instanceTypeDetails.volumeDetailsList.add(volumeDetails);
          }
          instanceTypeDetails.setDefaultMountPaths();
        }
      }

      InstanceType.upsert(
          provider.getUuid(), instanceTypeCode, numCores, memSizeGb, instanceTypeDetails);

      log.debug(
          "Loaded OCI instance type {} with {} cores and {} GB memory",
          instanceTypeCode,
          numCores,
          memSizeGb);
    }
  }

  // YAML is the min valid Flex SKU. ListShapes local_disks is the family max, not that SKU.
  private static double resourceFromApi(
      String instanceTypeCode, JsonNode node, InstanceType existing, String field) {
    double api = node != null ? node.path(field).asDouble(0) : 0;
    if (existing != null && isLocalNvmeFlex(instanceTypeCode)) {
      Double yamlVal = "numCores".equals(field) ? existing.getNumCores() : existing.getMemSizeGB();
      if (yamlVal != null) {
        return yamlVal;
      }
    }
    return api;
  }

  private static InstanceTypeDetails detailsFromApi(
      String instanceTypeCode, JsonNode node, InstanceType existing) {
    if (existing != null && isNvmeDetails(existing.getInstanceTypeDetails())) {
      return existing.getInstanceTypeDetails();
    }
    if (isLocalNvmeFlex(instanceTypeCode)) {
      return nvmeDetails(1, FALLBACK_NVME_SIZE_GB);
    }
    int localDisks = node != null ? node.path("localDisks").asInt(0) : 0;
    double localDisksInGbs = node != null ? node.path("localDisksInGbs").asDouble(0) : 0;
    if (isLocalNvmeShape(instanceTypeCode) && localDisks > 0) {
      int size = perDiskSizeGb(localDisks, localDisksInGbs);
      return nvmeDetails(localDisks, size > 0 ? size : FALLBACK_NVME_SIZE_GB);
    }
    if (isLocalNvmeShape(instanceTypeCode)) {
      return nvmeDetails(1, FALLBACK_NVME_SIZE_GB);
    }
    return InstanceTypeDetails.createOCIDefault();
  }

  private static boolean isLocalNvmeShape(String shape) {
    return shape != null && (shape.contains("DenseIO") || shape.contains("HPC"));
  }

  private static boolean isLocalNvmeFlex(String shape) {
    return isLocalNvmeShape(shape) && shape.contains("Flex");
  }

  private static boolean isNvmeDetails(InstanceTypeDetails details) {
    return details != null
        && details.volumeDetailsList != null
        && !details.volumeDetailsList.isEmpty()
        && details.volumeDetailsList.get(0).volumeType == InstanceType.VolumeType.NVME;
  }

  private static InstanceTypeDetails nvmeDetails(int volumeCount, int volumeSizeGB) {
    InstanceTypeDetails details = new InstanceTypeDetails();
    details.setVolumeDetailsList(volumeCount, volumeSizeGB, InstanceType.VolumeType.NVME);
    return details;
  }

  private static int perDiskSizeGb(int localDisks, double localDisksInGbs) {
    if (localDisks <= 0) {
      return (int) Math.round(localDisksInGbs);
    }
    int size = (int) Math.round(localDisksInGbs / localDisks);
    return size > 0 ? size : (int) Math.round(localDisksInGbs);
  }
}
