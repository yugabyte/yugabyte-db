/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */


package com.yugabyte.yw.cloud;

import java.time.Instant;
import java.time.format.DateTimeFormatter;
import java.util.Iterator;
import java.util.List;
import java.util.UUID;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.PriceComponent.PriceDetails;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import play.libs.Json;
import play.mvc.Result;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.BAD_REQUEST;

@Singleton
public class GCPInitializer extends AbstractInitializer {

  private Provider provider;

  /**
   * This will construct and store a PriceComponent object for the InstanceType in each Region.
   * Price for the Instance will be normalized to pricePerHour, which is the pricePerUnit.
   * Expected format of instanceTypeToDetailsMap json:
   *   {
   *     "prices": {
   *       "us-west1": [
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
   *   }
   *
   * @param instanceTypeCode Code of the instanceType (e.g. n1-standard-32).
   * @param instanceTypeToDetailsMap Json map of instanceType details for each region we care about.
   */
  private void storeInstancePriceComponents(String instanceTypeCode,
                                           JsonNode instanceTypeToDetailsMap) {
    JsonNode regionToPriceMap = instanceTypeToDetailsMap.get("prices");
    String now = DateTimeFormatter.ISO_INSTANT.format(Instant.now());

    Iterator<String> regionCodeItr = regionToPriceMap.fieldNames();
    while (regionCodeItr.hasNext()) {
      String regionCode = regionCodeItr.next();

      JsonNode osToPriceList = regionToPriceMap.get(regionCode);
      for (JsonNode osJson: osToPriceList) {
        PriceDetails priceDetails = new PriceDetails();
        priceDetails.unit = PriceDetails.Unit.Hours;
        priceDetails.pricePerUnit = osJson.get("price").asDouble();
        priceDetails.pricePerHour = priceDetails.pricePerUnit;
        priceDetails.pricePerDay = priceDetails.pricePerHour * 24.0;
        priceDetails.pricePerMonth = priceDetails.pricePerDay * 30.0;
        priceDetails.currency = PriceDetails.Currency.USD;
        priceDetails.effectiveDate = now;
        priceDetails.description = instanceTypeToDetailsMap.get("description").asText();

        PriceComponent.upsert(provider.code, regionCode, instanceTypeCode, priceDetails);
      }
    }
  }

  /**
   * Entry point to initialize GCP. This will create the various InstanceTypes and their
   * corresponding PriceComponents per Region for GCP.
   *
   * @param customerUUID UUID of the Customer.
   * @param providerUUID UUID of the Customer's configured GCP.
   * @return A response result that can be returned to the user to indicate success/failure.
   */
  @Override
  public Result initialize(UUID customerUUID, UUID providerUUID) {
    try {
      // Validate input
      Customer customer = Customer.get(customerUUID);
      if (customer == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
      }
      provider = Provider.get(customerUUID, providerUUID);
      if (provider == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
      }

      // Get some basic info
      List<Region> regionList = Region.fetchValidRegions(customerUUID, providerUUID, 0);
      Common.CloudType cloudType = Common.CloudType.valueOf(provider.code);

      JsonNode instanceTypes = cloudQueryHelper.getInstanceTypes(
        regionList, Json.stringify(Json.toJson(provider.getCloudParams())));

      // Iterate through each instance type and store their details in the db.
      Iterator<String> itr = instanceTypes.fieldNames();
      while (itr.hasNext()) {
        String instanceTypeCode = itr.next();
        JsonNode instanceTypeToDetailsMap = instanceTypes.get(instanceTypeCode);

        // Set up instanceTypeDetails.
        InstanceTypeDetails instanceTypeDetails = InstanceTypeDetails.createGCPDefault();
        if (instanceTypeToDetailsMap.get("isShared").asBoolean()) {
          instanceTypeDetails.tenancy = PublicCloudConstants.Tenancy.Shared;
        } else {
          instanceTypeDetails.tenancy = PublicCloudConstants.Tenancy.Dedicated;
        }

        // Store instanceType and corresponding priceComponents in the db.
        InstanceType.upsert(provider.code,
                            instanceTypeCode,
                            instanceTypeToDetailsMap.get("numCores").asInt(),
                            instanceTypeToDetailsMap.get("memSizeGb").asDouble(),
                            instanceTypeDetails);
        storeInstancePriceComponents(instanceTypeCode, instanceTypeToDetailsMap);
      }
    } catch (Exception e) {
      LOG.error("GCP Initialize failed", e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }

    return ApiResponse.success("GCP Initialized");
  }
}
