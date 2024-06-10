// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import play.libs.Json;

@Singleton
public class OnPremValidator extends ProviderFieldsValidator {

  private final RuntimeConfGetter runtimeConfigGetter;
  private final String zoneNameRegex = "^(?![_\\-])[\\w\\s\\-]+(?<![_\\-\\s])$";

  @Inject
  public OnPremValidator(BeanValidator beanValidator, RuntimeConfGetter runtimeConfigGetter) {
    super(beanValidator, runtimeConfigGetter);
    this.runtimeConfigGetter = runtimeConfigGetter;
  }

  @Override
  public void validate(Provider provider) {
    JsonNode processedProvider = Util.addJsonPathToLeafNodes(Json.toJson(provider));
    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();

    validatePrivateKeys(provider, processedProvider, validationErrorsMap);
    ArrayNode regionArrayJson = (ArrayNode) processedProvider.get("regions");

    if (provider.getRegions() != null && !provider.getRegions().isEmpty()) {
      int regionIndex = 0;
      for (Region region : provider.getRegions()) {
        JsonNode regionJson = regionArrayJson.get(regionIndex++);
        if (region.getZones() != null && !region.getZones().isEmpty()) {
          int zoneIndex = 0;
          ArrayNode zoneArrayJson = (ArrayNode) regionJson.get("zones");
          // Validate the zone names here.
          for (AvailabilityZone zone : region.getZones()) {
            validateAgainstRegex(
                zone.getName(),
                zoneArrayJson.get(zoneIndex).get("name").get("jsonPath").asText(),
                validationErrorsMap);
            validateAgainstRegex(
                zone.getCode(),
                zoneArrayJson.get(zoneIndex).get("code").get("jsonPath").asText(),
                validationErrorsMap);
            zoneIndex++;
          }
        }
      }
    }

    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, Json.toJson(provider));
    }
  }

  private void validateAgainstRegex(
      String value, String path, SetMultimap<String, String> validationErrorsMap) {
    if (!value.matches(zoneNameRegex)) {
      validationErrorsMap.put(
          path,
          String.format("%s, cannot contain any special characters except '-' and '_'.", value));
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
    JsonNode processedZone = Util.addJsonPathToLeafNodes(Json.toJson(zone));
    validateAgainstRegex(
        zone.getName(), processedZone.get("name").get("jsonPath").asText(), validationErrorsMap);
    validateAgainstRegex(
        zone.getCode(), processedZone.get("code").get("jsonPath").asText(), validationErrorsMap);
    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, Json.toJson(zone));
    }
  }
}
