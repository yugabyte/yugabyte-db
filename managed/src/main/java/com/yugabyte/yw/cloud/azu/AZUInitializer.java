/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud.azu;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.AbstractInitializer;
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
import java.util.UUID;
import play.libs.Json;

@Singleton
public class AZUInitializer extends AbstractInitializer {

  private void storeInstancePriceComponents(
      InitializationContext context, String instanceTypeCode, JsonNode instanceTypeToDetailsMap) {
    JsonNode regionToPriceMap = instanceTypeToDetailsMap.get("prices");
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
   * Entry point to initialize AZU. This will create the various InstanceTypes and their
   * corresponding PriceComponents per Region for AZU.
   *
   * @param customerUUID UUID of the Customer.
   * @param providerUUID UUID of the Customer's configured AZU.
   */
  @Override
  public void initialize(UUID customerUUID, UUID providerUUID) {
    // Validate input
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    InitializationContext context = new InitializationContext(provider);

    List<Region> regionList = Region.fetchValidRegions(customerUUID, providerUUID, 0);

    JsonNode instanceTypes =
        getCloudQueryHelper()
            .getInstanceTypes(regionList, Json.stringify(Json.toJson(provider.getCloudParams())));

    Iterator<String> itr = instanceTypes.fieldNames();

    while (itr.hasNext()) {

      String instanceTypeCode = itr.next();
      JsonNode instanceTypeToDetailsMap = instanceTypes.get(instanceTypeCode);

      InstanceTypeDetails instanceTypeDetails = InstanceTypeDetails.createAZUDefault();

      InstanceType.upsert(
          provider.getUuid(),
          instanceTypeCode,
          instanceTypeToDetailsMap.get("numCores").asInt(),
          instanceTypeToDetailsMap.get("memSizeGb").asDouble(),
          instanceTypeDetails);
      storeInstancePriceComponents(context, instanceTypeCode, instanceTypeToDetailsMap);
    }
  }
}
