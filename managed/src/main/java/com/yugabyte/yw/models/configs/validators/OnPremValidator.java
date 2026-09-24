// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.SetMultimap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
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

    validateFederatedIamFields(provider);
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

  // Format rules live on CrossCloudFederationTarget so this and GCPProviderValidator accept
  // exactly the same values; validating at save keeps a malformed value from failing later on
  // every node in ManageCloudFederation.
  /** Storage clouds federation can currently reach. */
  private static final Set<CloudType> SUPPORTED_TARGET_CLOUDS =
      ImmutableSet.of(CloudType.aws, CloudType.gcp);

  /**
   * Validates every configured target on its own: a provider may carry several, and having one
   * usable entry does not excuse a malformed other.
   */
  private void validateFederatedIamFields(Provider provider) {
    OnPremCloudInfo info = CloudInfoInterface.get(provider);
    if (info == null || !info.enableFederatedIam) {
      return;
    }
    List<CrossCloudFederationTarget> targets = info.getEffectiveFederationTargets();
    if (targets.isEmpty()) {
      throwBeanProviderValidatorError(
          "FEDERATED_IAM",
          "Federated IAM is enabled but no target is configured. Add a crossCloudFederationTargets"
              + " entry per storage cloud this provider's nodes must reach.",
          null);
    }
    Set<CloudType> seen = new HashSet<>();
    for (CrossCloudFederationTarget target : targets) {
      if (target.targetCloud == null || !SUPPORTED_TARGET_CLOUDS.contains(target.targetCloud)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_TARGET_CLOUD",
            String.format(
                "Unsupported federated IAM target cloud %s. Supported: %s.",
                target.targetCloud, SUPPORTED_TARGET_CLOUDS),
            null);
      }
      // One entry per cloud: two would make "the settings for reaching S3" ambiguous, and the
      // node picks its entry by target cloud alone.
      if (!seen.add(target.targetCloud)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_TARGET_CLOUD",
            String.format("Duplicate federated IAM target for cloud %s.", target.targetCloud),
            null);
      }
      if (!CrossCloudFederationTarget.isValidAudience(target.audience)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_AUDIENCE",
            String.format(
                "Federated IAM audience for target %s is missing or contains unsupported"
                    + " characters.",
                target.targetCloud),
            null);
      }
      // Reaching S3 is only possible with a role to assume, so a half-configured entry is an
      // error rather than something to quietly ignore.
      if (target.targetCloud == CloudType.aws
          && !CrossCloudFederationTarget.isValidRoleArn(target.roleArn)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_ROLE_ARN",
            "Federated IAM role ARN for the aws target is missing or malformed. Expected"
                + " arn:aws:iam::<12-digit-account>:role/<role-name>.",
            null);
      }
    }
  }
}
