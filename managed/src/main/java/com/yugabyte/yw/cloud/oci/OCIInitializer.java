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
import java.time.Instant;
import java.time.format.DateTimeFormatter;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Singleton
public class OCIInitializer extends AbstractInitializer {

  @Inject private ConfigHelper configHelper;

  private void storeInstancePriceComponents(
      InitializationContext context, String instanceTypeCode, JsonNode instanceTypeToDetailsMap) {
    JsonNode regionToPriceMap = instanceTypeToDetailsMap.get("prices");
    if (regionToPriceMap == null) {
      return;
    }
    String now = DateTimeFormatter.ISO_INSTANT.format(Instant.now());

    Iterator<String> regionCodeItr = regionToPriceMap.fieldNames();
    while (regionCodeItr.hasNext()) {
      String regionCode = regionCodeItr.next();
      PriceDetails priceDetails = new PriceDetails();
      priceDetails.unit = PriceDetails.Unit.Hours;
      priceDetails.pricePerUnit = regionToPriceMap.get(regionCode).asDouble();
      priceDetails.pricePerHour = priceDetails.pricePerUnit;
      priceDetails.pricePerDay = priceDetails.pricePerHour * 24.0;
      priceDetails.pricePerMonth = priceDetails.pricePerDay * 30.0;
      priceDetails.currency = PriceDetails.Currency.USD;
      priceDetails.effectiveDate = now;

      PriceComponent.upsert(
          context.getProvider().getUuid(), regionCode, instanceTypeCode, priceDetails);
    }
  }

  /**
   * Entry point to initialize OCI. This will create the various InstanceTypes and their
   * corresponding PriceComponents per Region for OCI.
   *
   * @param customerUUID UUID of the Customer.
   * @param providerUUID UUID of the Customer's configured OCI.
   */
  @Override
  public void initialize(UUID customerUUID, UUID providerUUID) {
    // Validate input
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    InitializationContext context = new InitializationContext(provider);

    // Load base instance types from YAML first to ensure common types are available
    log.info("Loading OCI instance types from YAML metadata for provider {}", providerUUID);
    initializeFromYamlMetadata(provider);

    List<Region> regionList = Region.fetchValidRegions(customerUUID, providerUUID, 0);

    JsonNode instanceTypes = null;
    try {
      instanceTypes =
          getCloudQueryHelper()
              .getInstanceTypes(regionList, Json.stringify(Json.toJson(provider.getCloudParams())));
    } catch (Exception e) {
      log.error("Failed to fetch instance types from OCI API for provider {}", providerUUID, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Failed to fetch instance types from OCI API for provider "
              + providerUUID
              + ". "
              + e.getMessage());
    }

    if (instanceTypes == null || instanceTypes.isEmpty()) {
      log.info("No additional instance types returned from OCI API for provider {}", providerUUID);
      return;
    }

    log.info(
        "Adding {} instance types from OCI API for provider {}",
        instanceTypes.size(),
        providerUUID);

    Iterator<String> itr = instanceTypes.fieldNames();

    while (itr.hasNext()) {
      String instanceTypeCode = itr.next();
      JsonNode instanceTypeToDetailsMap = instanceTypes.get(instanceTypeCode);

      InstanceTypeDetails instanceTypeDetails = InstanceTypeDetails.createOCIDefault();

      int numCores =
          instanceTypeToDetailsMap.has("numCores")
              ? instanceTypeToDetailsMap.get("numCores").asInt()
              : 0;
      double memSizeGb =
          instanceTypeToDetailsMap.has("memSizeGb")
              ? instanceTypeToDetailsMap.get("memSizeGb").asDouble()
              : 0;

      InstanceType.upsert(
          provider.getUuid(), instanceTypeCode, numCores, memSizeGb, instanceTypeDetails);
      storeInstancePriceComponents(context, instanceTypeCode, instanceTypeToDetailsMap);
    }
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

      // Parse instanceTypeDetails from YAML if present
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
}
